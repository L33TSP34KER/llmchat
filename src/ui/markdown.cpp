#include "markdown.h"
#include "text_util.h"
#include <algorithm>
#include <cctype>
#include <sstream>
#include <unordered_map>

// -------------------------------------------------------------------
// LaTeX → Unicode converter
// -------------------------------------------------------------------
static std::string utf8_encode(unsigned int cp) {
    std::string r;
    if (cp < 0x80) { r += (char)cp; }
    else if (cp < 0x800) { r += (char)(0xC0 | (cp>>6)); r += (char)(0x80 | (cp&0x3F)); }
    else if (cp < 0x10000) { r += (char)(0xE0 | (cp>>12)); r += (char)(0x80 | ((cp>>6)&0x3F)); r += (char)(0x80 | (cp&0x3F)); }
    else { r += (char)(0xF0 | (cp>>18)); r += (char)(0x80 | ((cp>>12)&0x3F)); r += (char)(0x80 | ((cp>>6)&0x3F)); r += (char)(0x80 | (cp&0x3F)); }
    return r;
}

static std::string to_superscript(char c) {
    if (c>='0' && c<='9') return utf8_encode(0x2070+(c-'0'));
    if (c=='i') return "\xc2\xb9";
    if (c=='n') return "\xe2\x81\xbf";
    if (c=='+') return "\xe2\x81\xaa";
    if (c=='-') return "\xe2\x81\xad";
    if (c=='(') return "\xe2\x81\xae";
    if (c==')') return "\xe2\x81\xaf";
    return std::string(1,c);
}

static std::string to_subscript(char c) {
    if (c>='0' && c<='9') return utf8_encode(0x2080+(c-'0'));
    if (c=='+') return "\xe2\x82\x9a";
    if (c=='-') return "\xe2\x82\x9b";
    if (c=='(') return "\xe2\x82\x9c";
    if (c==')') return "\xe2\x82\x9d";
    return std::string(1,c);
}

static std::string bold_upper(char c) { return utf8_encode(0x1D400+(c-'A')); }
static std::string bold_lower(char c) { return utf8_encode(0x1D41A+(c-'a')); }
static std::string italic_upper(char c) { return utf8_encode(0x1D434+(c-'A')); }
static std::string italic_lower(char c) { return utf8_encode(0x1D44E + (c - 'a')); }

static std::string blackboard(char c) {
    switch (c) {
        case 'C': return "\xe2\x84\x82"; case 'H': return "\xe2\x84\x8d";
        case 'N': return "\xe2\x84\x95"; case 'P': return "\xe2\x84\x99";
        case 'Q': return "\xe2\x84\x9a"; case 'R': return "\xe2\x84\x9d";
        case 'Z': return "\xe2\x84\xa4"; case 'B': return "\xe2\x84\x85";
        default: return std::string(1,c);
    }
}

static std::string script(char c) {
    if (c>='A' && c<='Z') {
        const char* map[] = {
            "\xf0\x9d\x92\x9c","\xe2\x84\xac","\xf0\x9d\x92\x9e","\xf0\x9d\x92\x9f",
            "\xe2\x84\xb0","\xe2\x84\xb1","\xf0\x9d\x92\xa2","\xe2\x84\x8b",
            "\xe2\x84\x90","\xf0\x9d\x92\xa5","\xf0\x9d\x92\xa6","\xe2\x84\x92",
            "\xe2\x84\xb3","\xf0\x9d\x92\xa9","\xf0\x9d\x92\xaa","\xf0\x9d\x92\xab",
            "\xf0\x9d\x92\xac","\xe2\x84\x9b","\xf0\x9d\x92\xad","\xf0\x9d\x92\xae",
            "\xf0\x9d\x92\xaf","\xf0\x9d\x92\xb0","\xf0\x9d\x92\xb1","\xf0\x9d\x92\xb2",
            "\xf0\x9d\x92\xb3","\xf0\x9d\x92\xb4"
        };
        return map[c-'A'];
    }
    return std::string(1,c);
}

static std::string fraktur(char c) {
    if (c>='A' && c<='Z') return utf8_encode(0x1D504+(c-'A'));
    if (c>='a' && c<='z') return utf8_encode(0x1D51E + (c - 'a'));
    return std::string(1,c);
}

static std::string read_braced(const std::string& s, size_t& i) {
    if (i >= s.size() || s[i] != '{') return "";
    int d = 1;
    size_t start = i + 1;
    size_t end = start;
    while (end < s.size() && d > 0) {
        if (s[end] == '{') d++;
        else if (s[end] == '}') d--;
        if (d > 0) end++;
    }
    std::string inner = s.substr(start, end - start);
    i = end + 1;
    return inner;
}

static std::string latex_to_unicode(const std::string& latex);

static std::string format_mathbf(const std::string& inner) {
    std::string r;
    for (char c : inner) {
        if (c >= 'A' && c <= 'Z') r += bold_upper(c);
        else if (c >= 'a' && c <= 'z') r += bold_lower(c);
        else r += c;
    }
    return r;
}

static std::string format_mathit(const std::string& inner) {
    std::string r;
    for (char c : inner) {
        if (c >= 'A' && c <= 'Z') r += italic_upper(c);
        else if (c >= 'a' && c <= 'z') r += italic_lower(c);
        else r += c;
    }
    return r;
}

static const std::unordered_map<std::string, std::string>& latex_cmds() {
    static const std::unordered_map<std::string, std::string> m = {
        {"alpha","\xce\xb1"},{"beta","\xce\xb2"},{"gamma","\xce\xb3"},{"delta","\xce\xb4"},
        {"epsilon","\xce\xb5"},{"zeta","\xce\xb6"},{"eta","\xce\xb7"},{"theta","\xce\xb8"},
        {"iota","\xce\xb9"},{"kappa","\xce\xba"},{"lambda","\xce\xbb"},{"mu","\xce\xbc"},
        {"nu","\xce\xbd"},{"xi","\xce\xbe"},{"pi","\xcf\x80"},{"rho","\xcf\x81"},
        {"sigma","\xcf\x83"},{"tau","\xcf\x84"},{"upsilon","\xcf\x85"},{"phi","\xcf\x86"},
        {"chi","\xcf\x87"},{"psi","\xcf\x88"},{"omega","\xcf\x89"},
        {"varepsilon","\xce\xb5"},{"vartheta","\xcf\x91"},{"varpi","\xcf\x96"},
        {"varrho","\xcf\xb1"},{"varsigma","\xcf\x82"},{"varphi","\xcf\x95"},
        {"Gamma","\xce\x93"},{"Delta","\xce\x94"},{"Theta","\xce\x98"},
        {"Lambda","\xce\x9b"},{"Xi","\xce\x9e"},{"Pi","\xce\xa0"},
        {"Sigma","\xce\xa3"},{"Phi","\xce\xa6"},{"Psi","\xce\xa8"},{"Omega","\xce\xa9"},
        {"ell","\xe2\x84\x93"},{"hbar","\xe2\x84\x8f"},{"hslash","\xe2\x84\x8f"},
        {"partial","\xe2\x88\x82"},{"nabla","\xe2\x88\x87"},{"infty","\xe2\x88\x9e"},
        {"prime","\xe2\x80\xb2"},{"dprime","\xe2\x80\xb3"},{"trprime","\xe2\x80\xb4"},
        {"emptyset","\xe2\x88\x85"},{"varnothing","\xe2\x88\x85"},
        {"imath","\xc4\xb1"},{"jmath","\xe1\xb7\x8a"},
        {"aleph","\xe2\x84\xb5"},{"beth","\xe2\x84\xb6"},{"gimel","\xe2\x84\xb7"},
        {"Re","\xe2\x84\x9c"},{"Im","\xe2\x84\x91"},
        {"wp","\xe2\x84\x98"},{"ell","\xe2\x84\x93"},
        {"sum","\xe2\x88\x91"},{"prod","\xe2\x88\x8f"},{"coprod","\xe2\x88\x90"},
        {"int","\xe2\x88\xab"},{"iint","\xe2\x88\xac"},{"iiint","\xe2\x88\xad"},
        {"oint","\xe2\x88\xae"},{"ointctrclockwise","\xe2\x88\xaf"},
        {"sqrt","\xe2\x88\x9a"},{"surd","\xe2\x88\x9a"},
        {"angle","\xe2\x88\xa0"},{"measuredangle","\xe2\x88\xa1"},{"sphericalangle","\xe2\x88\xa2"},
        {"triangle","\xe2\x96\xb3"},{"triangledown","\xe2\x96\xbd"},
        {"forall","\xe2\x88\x80"},{"exists","\xe2\x88\x83"},{"nexists","\xe2\x88\x84"},
        {"top","\xe2\x8a\xa5"},{"bot","\xe2\x8a\xa5"},{"perp","\xe2\x9f\xb5"},
        {"neg","\xc2\xac"},{"lnot","\xc2\xac"},
        {"wedge","\xe2\x88\xa7"},{"vee","\xe2\x88\xa8"},{"lor","\xe2\x88\xa8"},{"land","\xe2\x88\xa7"},
        {"oplus","\xe2\x8a\x95"},{"ominus","\xe2\x8a\x96"},{"otimes","\xe2\x8a\x97"},
        {"oslash","\xe2\x8a\x98"},{"odot","\xe2\x8a\x99"},{"circ","\xe2\x88\x98"},
        {"bullet","\xe2\x80\xa2"},{"cdot","\xc2\xb7"},{"cdots","\xe2\x8b\xaf"},
        {"ldots","\xe2\x80\xa6"},{"ddots","\xe2\x8b\xb1"},{"vdots","\xe2\x8b\xae"},
        {"times","\xc3\x97"},{"div","\xc3\xb7"},{"pm","\xc2\xb1"},{"mp","\xe2\x88\x93"},
        {"ast","\xe2\x88\x97"},{"star","\xe2\x98\x86"},{"starred","\xe2\x98\x85"},
        {"propto","\xe2\x88\x9d"},{"sim","\xe2\x88\xbc"},{"simeq","\xe2\x89\x83"},
        {"approx","\xe2\x89\x88"},{"cong","\xe2\x89\x85"},{"equiv","\xe2\x89\xa1"},
        {"doteq","\xe2\x89\x90"},{"neq","\xe2\x89\xa0"},{"ne","\xe2\x89\xa0"},
        {"leq","\xe2\x89\xa4"},{"ge","\xe2\x89\xa5"},{"geq","\xe2\x89\xa5"},
        {"ll","\xe2\x89\xaa"},{"gg","\xe2\x89\xab"},{"prec","\xe2\x89\xba"},{"succ","\xe2\x89\xbb"},
        {"subset","\xe2\x8a\x82"},{"supset","\xe2\x8a\x83"},
        {"subseteq","\xe2\x8a\x86"},{"supseteq","\xe2\x8a\x87"},
        {"sqsubset","\xe2\x8a\x8f"},{"sqsupset","\xe2\x8a\x90"},
        {"in","\xe2\x88\x88"},{"notin","\xe2\x88\x89"},{"ni","\xe2\x88\x8b"},{"owns","\xe2\x88\x8b"},
        {"cup","\xe2\x88\xaa"},{"cap","\xe2\x88\xa9"},{"sqcup","\xe2\x8a\x94"},{"sqcap","\xe2\x8a\x93"},
        {"uplus","\xe2\x8a\x8e"},{"setminus","\xe2\x88\x96"},{"smallsetminus","\xe2\x88\x96"},
        {"rightarrow","\xe2\x86\x92"},{"to","\xe2\x86\x92"},{"Rightarrow","\xe2\x87\x92"},
        {"implies","\xe2\x9f\xb9"},{"leftarrow","\xe2\x86\x90"},{"Leftarrow","\xe2\x87\x90"},
        {"leftrightarrow","\xe2\x86\x94"},{"Leftrightarrow","\xe2\x87\x94"},
        {"iff","\xe2\x9f\xba"},{"mapsto","\xe2\x86\xa6"},{"longmapsto","\xe2\x9f\xbc"},
        {"uparrow","\xe2\x86\x91"},{"downarrow","\xe2\x86\x93"},{"updownarrow","\xe2\x86\x95"},
        {"Uparrow","\xe2\x87\x91"},{"Downarrow","\xe2\x87\x93"},{"Updownarrow","\xe2\x87\x95"},
        {"nearrow","\xe2\x86\x97"},{"searrow","\xe2\x86\x98"},{"nwarrow","\xe2\x86\x96"},{"swarrow","\xe2\x86\x99"},
        {"longrightarrow","\xe2\x9f\xb6"},{"longleftarrow","\xe2\x9f\xb5"},
        {"hookrightarrow","\xe2\x86\xaa"},{"hookleftarrow","\xe2\x86\xa9"},
        {"rightharpoonup","\xe2\x87\x8c"},{"rightharpoondown","\xe2\x87\x8d"},
        {"leftharpoonup","\xe2\x87\x8b"},{"leftharpoondown","\xe2\x87\x8a"},
        {"rightleftharpoons","\xe2\x87\x8c"},
        {"degree","\xc2\xb0"},{"cdot","\xc2\xb7"},
        {"colon","\xc2\xb7"},{"vert","\xe2\x88\xa3"},{"Vert","\xe2\x88\xa5"},
        {"|","\xe2\x88\xa5"},{"parallel","\xe2\x88\xa5"},{"perp","\xe2\x8a\xa5"},
        {"dag","\xe2\x80\xa0"},{"ddag","\xe2\x80\xa1"},{"P","\xc2\xb6"},{"S","\xc2\xa7"},
        {"flat","\xe2\x99\xad"},{"natural","\xe2\x99\xae"},{"sharp","\xe2\x99\xaf"},
        {"clubsuit","\xe2\x99\xa3"},{"diamondsuit","\xe2\x99\xa2"},{"heartsuit","\xe2\x99\xa1"},{"spadesuit","\xe2\x99\xa0"},
        {"mod",":"},
        {"space"," "},{"quad","  "},{"qquad","    "},
        {"dots","\xe2\x80\xa6"},{"cdots","\xe2\x8b\xaf"},
        {"colon",":"},{"textbackslash","\\"},
        {"big",""},{"Big",""},{"bigg",""},{"Bigg",""},
        {"bigl",""},{"Bigl",""},{"biggl",""},{"Biggl",""},
        {"bigr",""},{"Bigr",""},{"biggr",""},{"Biggr",""},
        {"left",""},{"right",""},
        {"displaystyle",""},{"textstyle",""},
    };
    return m;
}

static std::string latex_to_unicode(const std::string& latex) {
    std::string r;
    size_t i = 0;

    auto is_alpha = [](char c) { return (c>='a'&&c<='z')||(c>='A'&&c<='Z'); };

    while (i < latex.size()) {
        if (latex[i] == '\\') {
            if (i+1 >= latex.size()) { r += '\\'; i++; continue; }
            char n = latex[i+1];
            if (n == ' ' || n == ',') { r += ' '; i += 2; continue; }
            if (n == ';') { r += "  "; i += 2; continue; }
            if (n == '!') { r += ""; i += 2; continue; } // negative thin space
            if (n == '{' || n == '}') { i += 2; continue; } // \{ \} → just { }
            if (n == '(' || n == ')' || n == '[' || n == ']') {
                r += n; i += 2; continue;
            }
            if (n == '\\') {
                size_t skip = i+2;
                if (skip < latex.size() && latex[skip] == '\\') skip++;
                if (skip < latex.size() && latex[skip] == '[') { /* ignored */ i = skip+1; continue; }
                if (skip < latex.size() && latex[skip] == ']') { /* ignored */ i = skip+1; continue; }
                r += '\\'; i++; continue;
            }

            // \pmod{...}
            if (latex.substr(i+1, 4) == "pmod" && i+5 < latex.size() && latex[i+5] == '{') {
                r += " (mod ";
                i += 6;
                r += latex_to_unicode(read_braced(latex, i));
                r += ")";
                continue;
            }
            // \mod{...}
            if (latex.substr(i+1, 3) == "mod" && i+4 < latex.size() && latex[i+4] == '{') {
                r += " mod ";
                i += 5;
                r += latex_to_unicode(read_braced(latex, i));
                continue;
            }
            // \bmod 
            if (latex.substr(i+1, 4) == "bmod") {
                r += " mod ";
                i += 5;
                continue;
            }
            // \binom - parsed inline
            if (latex.substr(i+1, 5) == "binom" && i+6 < latex.size() && latex[i+6] == '{') {
                i += 7;
                std::string top = latex_to_unicode(read_braced(latex, i));
                if (i < latex.size() && latex[i] == '{') {
                    i++;
                    std::string bot = latex_to_unicode(read_braced(latex, i));
                    r += "(" + top + " choose " + bot + ")";
                } else { r += top; }
                continue;
            }

            size_t start = i + 1;
            size_t end = start;
            while (end < latex.size() && is_alpha(latex[end])) end++;
            std::string cmd = latex.substr(start, end - start);
            i = end;
            while (i < latex.size() && latex[i] == ' ') i++;

            if (cmd == "mathbf" || cmd == "bm") {
                if (i < latex.size() && latex[i] == '{') { i++; r += format_mathbf(read_braced(latex, i)); }
                continue;
            }
            if (cmd == "mathit") {
                if (i < latex.size() && latex[i] == '{') { i++; r += format_mathit(read_braced(latex, i)); }
                continue;
            }
            if (cmd == "mathbb") {
                if (i < latex.size() && latex[i] == '{') { i++;
                    std::string inner = read_braced(latex, i);
                    std::string br;
                    for (char c : inner) br += blackboard(c);
                    r += br;
                }
                continue;
            }
            if (cmd == "mathcal") {
                if (i < latex.size() && latex[i] == '{') { i++;
                    std::string inner = read_braced(latex, i);
                    std::string sc;
                    for (char c : inner) sc += script(c);
                    r += sc;
                }
                continue;
            }
            if (cmd == "mathfrak") {
                if (i < latex.size() && latex[i] == '{') { i++;
                    std::string inner = read_braced(latex, i);
                    std::string fk;
                    for (char c : inner) fk += fraktur(c);
                    r += fk;
                }
                continue;
            }
            if (cmd == "textrm" || cmd == "text" || cmd == "mathrm" || cmd == "operatorname") {
                if (i < latex.size() && latex[i] == '{') { i++; r += read_braced(latex, i); }
                continue;
            }
            if (cmd == "frac" || cmd == "dfrac" || cmd == "tfrac") {
                if (i < latex.size() && latex[i] == '{') { i++;
                    std::string num = latex_to_unicode(read_braced(latex, i));
                    if (i < latex.size() && latex[i] == '{') { i++;
                        std::string den = latex_to_unicode(read_braced(latex, i));
                        r += num + "/" + den;
                        continue;
                    }
                }
            }
            if (cmd == "sqrt") {
                if (i < latex.size() && latex[i] == '[') {
                    i++;
                    size_t nend = i;
                    while (nend < latex.size() && latex[nend] != ']') nend++;
                    r += "\xe2\x88\x9b"; // ∛
                    i = nend + 1;
                } else {
                    r += "\xe2\x88\x9a"; // √
                }
                if (i < latex.size() && latex[i] == '{') { i++; r += latex_to_unicode(read_braced(latex, i)); }
                continue;
            }
            if (cmd == "tilde" || cmd == "widetilde") {
                if (i < latex.size() && latex[i] == '{') { i++; r += "~" + latex_to_unicode(read_braced(latex, i)); }
                continue;
            }
            if (cmd == "hat" || cmd == "widehat") {
                if (i < latex.size() && latex[i] == '{') { i++; r += "^" + latex_to_unicode(read_braced(latex, i)); }
                continue;
            }
            if (cmd == "bar" || cmd == "overline") {
                if (i < latex.size() && latex[i] == '{') { i++; r += "\xc4\x80" + latex_to_unicode(read_braced(latex, i)); }
                continue;
            }
            if (cmd == "vec") {
                if (i < latex.size() && latex[i] == '{') { i++; r += "\xe2\x86\x92" + latex_to_unicode(read_braced(latex, i)); }
                continue;
            }
            if (cmd == "dot") {
                if (i < latex.size() && latex[i] == '{') { i++; r += "\xcb\x99" + latex_to_unicode(read_braced(latex, i)); }
                continue;
            }
            if (cmd == "ddot") {
                if (i < latex.size() && latex[i] == '{') { i++; r += "\xcb\x88" + latex_to_unicode(read_braced(latex, i)); }
                continue;
            }
            if (cmd == "color" || cmd == "textcolor") {
                if (i < latex.size() && latex[i] == '{') { i++; read_braced(latex, i); } // skip color
                if (i < latex.size() && latex[i] == '{') { i++; r += latex_to_unicode(read_braced(latex, i)); }
                continue;
            }
            if (cmd == "label" || cmd == "ref" || cmd == "cite" || cmd == "tag") {
                if (i < latex.size() && latex[i] == '{') { i++; read_braced(latex, i); }
                continue;
            }
            if (cmd == "begin" || cmd == "end") {
                if (i < latex.size() && latex[i] == '{') { i++; read_braced(latex, i); }
                continue;
            }
            if (cmd == "text") continue;
            if (cmd == "displaystyle" || cmd == "textstyle" || cmd == "scriptstyle" || cmd == "scriptscriptstyle") continue;
            if (cmd == "limits" || cmd == "nolimits") continue;
            if (cmd == "quad") { r += "  "; continue; }
            if (cmd == "qquad") { r += "    "; continue; }
            if (cmd == "enspace") { r += " "; continue; }
            if (cmd == "thinspace" || cmd == ",") { r += " "; continue; }
            if (cmd == "negthinspace" || cmd == "!") continue;
            if (cmd == "colon") { r += ":"; continue; }

            auto it = latex_cmds().find(cmd);
            if (it != latex_cmds().end()) {
                r += it->second;
            } else if (!cmd.empty()) {
                r += "\\" + cmd;
            }
            continue;
        }

        if (latex[i] == '^') {
            i++;
            std::string arg;
            if (i < latex.size() && latex[i] == '{') { i++; arg = read_braced(latex, i); }
            else if (i < latex.size()) { arg = latex.substr(i,1); i++; }
            for (char c : arg) r += to_superscript(c);
            continue;
        }

        if (latex[i] == '_') {
            i++;
            std::string arg;
            if (i < latex.size() && latex[i] == '{') { i++; arg = read_braced(latex, i); }
            else if (i < latex.size()) { arg = latex.substr(i,1); i++; }
            for (char c : arg) r += to_subscript(c);
            continue;
        }

        if (latex[i] == '{' || latex[i] == '}') { i++; continue; }
        if (latex[i] == '~') { r += ' '; i++; continue; }

        r += latex[i];
        i++;
    }
    return r;
}

// -------------------------------------------------------------------
// Inline parser — breaks a single line into segments
// -------------------------------------------------------------------
static std::vector<MdSeg> parse_inline(const std::string& line, bool in_list = false) {
    std::vector<MdSeg> out;
    std::string buf;
    size_t i = 0;

    auto flush = [&](MdSeg::Type t = MdSeg::NORMAL, int lvl = 0) {
        if (!buf.empty()) {
            out.push_back({t, buf, lvl});
            buf.clear();
        }
    };

    auto peek = [&](size_t off) -> char {
        size_t p = i + off;
        return p < line.size() ? line[p] : '\0';
    };

    while (i < line.size()) {
        // ~~strikethrough~~
        if (line[i] == '~' && peek(1) == '~') {
            flush();
            size_t end = line.find("~~", i + 2);
            if (end != std::string::npos) {
                buf = line.substr(i + 2, end - i - 2);
                flush(MdSeg::STRIKETHROUGH);
                i = end + 2;
                continue;
            }
        }
        // **bold**
        if (line[i] == '*' && peek(1) == '*') {
            flush();
            size_t end = line.find("**", i + 2);
            if (end != std::string::npos) {
                buf = line.substr(i + 2, end - i - 2);
                flush(MdSeg::BOLD);
                i = end + 2;
                continue;
            }
        }
        // *italic* (must not be **)
        if (line[i] == '*' && peek(1) != '*') {
            flush();
            size_t end = line.find("*", i + 1);
            if (end != std::string::npos && (end + 1 >= line.size() || line[end + 1] != '*')) {
                buf = line.substr(i + 1, end - i - 1);
                flush(MdSeg::ITALIC);
                i = end + 1;
                continue;
            }
        }
        // `code`
        if (line[i] == '`') {
            flush();
            size_t end = line.find("`", i + 1);
            if (end != std::string::npos) {
                buf = line.substr(i + 1, end - i - 1);
                flush(MdSeg::CODE);
                i = end + 1;
                continue;
            }
        }
        // \[ ... \] display math
        if (line[i] == '\\' && i + 1 < line.size() && line[i+1] == '[') {
            flush();
            size_t end = line.find("\\]", i + 2);
            if (end != std::string::npos) {
                buf = latex_to_unicode(line.substr(i + 2, end - i - 2));
                flush(MdSeg::DISPLAY_MATH);
                i = end + 2;
                continue;
            }
        }
        // $...$ inline math
        if (line[i] == '$' && (i == 0 || line[i-1] != '\\')) {
            // $$...$$ display math
            if (i + 1 < line.size() && line[i+1] == '$') {
                flush();
                size_t end = line.find("$$", i + 2);
                if (end != std::string::npos) {
                    buf = latex_to_unicode(line.substr(i + 2, end - i - 2));
                    flush(MdSeg::DISPLAY_MATH);
                    i = end + 2;
                    continue;
                }
            } else {
                flush();
                size_t end = line.find("$", i + 1);
                if (end != std::string::npos && (end + 1 >= line.size() || line[end+1] != '$')) {
                    buf = latex_to_unicode(line.substr(i + 1, end - i - 1));
                    flush(MdSeg::MATH);
                    i = end + 1;
                    continue;
                }
            }
        }
        // ![alt](url) image
        if (line[i] == '!' && peek(1) == '[') {
            flush();
            size_t close_bracket = line.find("](", i + 2);
            if (close_bracket != std::string::npos) {
                std::string alt = line.substr(i + 2, close_bracket - i - 2);
                size_t close_paren = line.find(")", close_bracket + 2);
                if (close_paren != std::string::npos) {
                    std::string url = line.substr(close_bracket + 2, close_paren - close_bracket - 2);
                    buf = alt + "\n" + url;
                    flush(MdSeg::IMAGE);
                    i = close_paren + 1;
                    continue;
                }
            }
        }

        buf += line[i];
        i++;
    }
    flush();
    return out;
}

// Check if a line is a list item (-, *, +, or 1. style)
static int detect_list_item(const std::string& line, bool& is_task, bool& task_done) {
    is_task = false;
    task_done = false;
    size_t start = line.find_first_not_of(" \t");
    if (start == std::string::npos) return 0;

    // Bullet: - * +
    if (line[start] == '-' || line[start] == '*' || line[start] == '+') {
        if (start + 1 < line.size() && line[start + 1] == ' ') {
            // Check for task list: - [ ] or - [x]
            size_t t = start + 2;
            if (t + 4 < line.size() && line[t] == '[' && line[t + 2] == ']' && line[t + 3] == ' ') {
                is_task = true;
                task_done = (line[t + 1] == 'x' || line[t + 1] == 'X');
                return 1;
            }
            return 1;
        }
    }

    // Numbered: 1. 2. etc.
    size_t p = start;
    while (p < line.size() && std::isdigit((unsigned char)line[p])) p++;
    if (p > start && p < line.size() && line[p] == '.' && p + 1 < line.size() && line[p + 1] == ' ') {
        return 2;
    }

    return 0;
}

// -------------------------------------------------------------------
// Public inline parser wrapper
// -------------------------------------------------------------------
std::vector<MdSeg> md_parse_inline(const std::string& line) {
    return parse_inline(line);
}

std::string md_latex_to_unicode(const std::string& latex) {
    return latex_to_unicode(latex);
}

// -------------------------------------------------------------------
// Main parser
// -------------------------------------------------------------------
std::vector<MdLine> md_parse(const std::string& text, int width) {
    std::vector<MdLine> out;
    if (width < 4) width = 4;

    std::istringstream stream(text);
    std::string raw_line;
    bool in_code_block = false;
    std::string code_buf;
    std::string current_code_lang_;

    while (std::getline(stream, raw_line)) {
        if (!raw_line.empty() && raw_line.back() == '\r')
            raw_line.pop_back();

        // -------------------------------------------------------------------
        // Code fences ``` or ~~~
        // -------------------------------------------------------------------
        if (raw_line.size() >= 3 && raw_line.substr(0, 3) == "```") {
            if (!in_code_block) {
                in_code_block = true;
                code_buf.clear();
                // Capture language (rest of line after ```)
                std::string lang = raw_line.substr(3);
                lang.erase(0, lang.find_first_not_of(" \t"));
                lang.erase(lang.find_last_not_of(" \t") + 1);
                current_code_lang_ = lang;
                continue;
            } else {
                in_code_block = false;
                MdLine ml;
                ml.is_code_block = true;
                ml.code_lang = current_code_lang_;
                ml.segs.push_back({MdSeg::CODE, code_buf, 0});
                out.push_back(ml);
                continue;
            }
        }
        if (in_code_block) {
            if (!code_buf.empty()) code_buf += "\n";
            code_buf += raw_line;
            continue;
        }

        // -------------------------------------------------------------------
        // Empty line
        // -------------------------------------------------------------------
        if (raw_line.empty()) {
            out.push_back({});
            continue;
        }

        // -------------------------------------------------------------------
        // Horizontal rule --- or ***
        // -------------------------------------------------------------------
        {
            std::string trimmed = raw_line;
            trimmed.erase(0, trimmed.find_first_not_of(" \t"));
            trimmed.erase(trimmed.find_last_not_of(" \t") + 1);
            bool is_hr = true;
            int dash_or_star = 0;
            for (char c : trimmed) {
                if (c == '-') dash_or_star++;
                else if (c == '*') dash_or_star++;
                else if (c == ' ') continue;
                else { is_hr = false; break; }
            }
            if (is_hr && dash_or_star >= 3) {
                { MdLine ml; ml.segs.push_back({MdSeg::HR, "", 0}); out.push_back(ml); }
                continue;
            }
        }

        // -------------------------------------------------------------------
        // Table detection: line starts and ends with |
        // -------------------------------------------------------------------
        std::string trimmed = raw_line;
        trimmed.erase(0, trimmed.find_first_not_of(" \t"));
        trimmed.erase(trimmed.find_last_not_of(" \t") + 1);
        if (trimmed.size() >= 2 && trimmed.front() == '|' && trimmed.back() == '|') {
            MdLine ml;
            ml.is_table = true;
            bool is_sep = true;
            std::string inner = trimmed.substr(1, trimmed.size() - 2);
            std::string cell;
            std::vector<std::string> cells;
            for (char c : inner) {
                if (c == '|') { cells.push_back(cell); cell.clear(); }
                else cell += c;
            }
            cells.push_back(cell);
            for (auto& c : cells) {
                std::string t = c;
                t.erase(0, t.find_first_not_of(" \t"));
                t.erase(t.find_last_not_of(" \t") + 1);
                for (char cc : t)
                    if (cc != '-' && cc != ':' && cc != ' ') is_sep = false;
                if (!t.empty() && t.find_first_not_of("-: ") != std::string::npos)
                    is_sep = false;
            }
            ml.segs.push_back({is_sep ? MdSeg::TABLE_SEP : MdSeg::TABLE_ROW, trimmed, 0});
            out.push_back(ml);
            continue;
        }

        // -------------------------------------------------------------------
        // Blockquote >
        // -------------------------------------------------------------------
        size_t start = raw_line.find_first_not_of(" \t");
        if (start != std::string::npos && raw_line[start] == '>') {
            std::string content = raw_line.substr(start + 1);
            content.erase(0, content.find_first_not_of(" \t"));
            auto segs = parse_inline(content);
            // Word-wrap blockquote content
            {
                int col = 0;
                MdLine current;
                current.is_blockquote = true;
                for (auto& seg : segs) {
                    std::string word;
                    for (size_t ci = 0; ci < seg.text.size();) {
                        size_t len = utf8_char_len((unsigned char)seg.text[ci]);
                        word = seg.text.substr(ci, len);
                        ci += len;
                        if (col + str_width(word) > width && col > 0) {
                            out.push_back(current);
                            current = MdLine();
                            current.is_blockquote = true;
                            col = 0;
                        }
                        if (col == 0 && word == " ") continue;
                        current.segs.push_back({seg.type, word, seg.level});
                        col += str_width(word);
                    }
                }
                if (!current.segs.empty())
                    out.push_back(current);
                else
                    out.push_back({});
            }
            continue;
        }

        // -------------------------------------------------------------------
        // Heading # ## ### #### ##### ######
        // -------------------------------------------------------------------
        start = raw_line.find_first_not_of(" \t");
        if (start != std::string::npos) {
            int level = 0;
            size_t p = start;
            while (p < raw_line.size() && raw_line[p] == '#') { level++; p++; }
            if (level >= 1 && level <= 6 && p < raw_line.size() && raw_line[p] == ' ') {
                std::string content = raw_line.substr(p + 1);
                auto segs = parse_inline(content);
                MdLine ml;
                for (auto& s : segs) {
                    s.type = MdSeg::HEADING;
                    s.level = level;
                    ml.segs.push_back(s);
                }
                // Word-wrap headings like regular lines
                {
                    int col = 0;
                    MdLine current;
                    for (auto& seg : ml.segs) {
                        std::string word;
                        for (size_t ci = 0; ci < seg.text.size();) {
                            size_t len = utf8_char_len((unsigned char)seg.text[ci]);
                            word = seg.text.substr(ci, len);
                            ci += len;
                            if (col + str_width(word) > width && col > 0) {
                                out.push_back(current);
                                current = MdLine();
                                col = 0;
                            }
                            if (col == 0 && word == " ") continue;
                            current.segs.push_back({seg.type, word, seg.level});
                            col += str_width(word);
                        }
                    }
                    if (!current.segs.empty())
                        out.push_back(current);
                    else
                        out.push_back({});
                }
                continue;
            }
        }

        // -------------------------------------------------------------------
        // List item / Task list
        // -------------------------------------------------------------------
        {
            bool is_task = false;
            bool task_done = false;
            int list_type = detect_list_item(raw_line, is_task, task_done);
            if (list_type > 0) {
                // Determine content start
                size_t content_start = raw_line.find_first_not_of(" \t");
                if (is_task) {
                    content_start += 6; // skip "- [ ] " or "- [x] "
                } else {
                    // skip bullet or number marker
                    if (list_type == 1) content_start += 2; // "- " or "* "
                    else {
                        content_start = raw_line.find('.', content_start) + 2; // "1. "
                    }
                }
                std::string content = raw_line.substr(content_start);
                auto segs = parse_inline(content);
                MdLine ml;
                if (is_task) {
                    MdSeg marker;
                    marker.type = task_done ? MdSeg::TASK_DONE : MdSeg::TASK_PENDING;
                    marker.text = task_done ? "[x] " : "[ ] ";
                    ml.segs.push_back(marker);
                } else {
                    MdSeg marker;
                    marker.type = MdSeg::LIST_ITEM;
                    marker.text = "  ";
                    ml.segs.push_back(marker);
                }
                for (auto& s : segs) ml.segs.push_back(s);
                // Word-wrap list items like regular lines
                int list_col = 0;
                MdLine wrapped;
                for (auto& seg : ml.segs) {
                    if (seg.type == MdSeg::LIST_ITEM || seg.type == MdSeg::TASK_DONE || seg.type == MdSeg::TASK_PENDING) {
                        if (list_col > 0 && list_col + str_width(seg.text) > width) {
                            out.push_back(wrapped);
                            wrapped = MdLine();
                            list_col = 0;
                        }
                        wrapped.segs.push_back(seg);
                        list_col += str_width(seg.text);
                        continue;
                    }
                    std::string word;
                    for (size_t ci = 0; ci < seg.text.size();) {
                        size_t len = utf8_char_len((unsigned char)seg.text[ci]);
                        word = seg.text.substr(ci, len);
                        ci += len;
                        if (list_col + str_width(word) > width && list_col > 0) {
                            out.push_back(wrapped);
                            wrapped = MdLine();
                            list_col = 0;
                        }
                        if (list_col == 0 && word == " ") continue;
                        wrapped.segs.push_back({seg.type, word, seg.level});
                        list_col += str_width(word);
                    }
                }
                if (wrapped.segs.empty())
                    wrapped = ml;
                out.push_back(wrapped);
                continue;
            }
        }

        // -------------------------------------------------------------------
        // Regular line with inline parsing + word wrapping
        // -------------------------------------------------------------------
        auto segs = parse_inline(raw_line);
        int col = 0;
        MdLine current;
        for (auto& seg : segs) {
            std::string word;
            for (size_t ci = 0; ci < seg.text.size();) {
                size_t len = utf8_char_len((unsigned char)seg.text[ci]);
                word = seg.text.substr(ci, len);
                ci += len;

                if (col + str_width(word) > width && col > 0) {
                    out.push_back(current);
                    current = MdLine();
                    col = 0;
                }
                if (col == 0 && word == " ") continue;
                current.segs.push_back({seg.type, word, seg.level});
                col += str_width(word);
            }
        }
        if (!current.segs.empty())
            out.push_back(current);
        else
            out.push_back({});
    }

    if (in_code_block) {
        MdLine ml;
        ml.is_code_block = true;
        ml.code_lang = current_code_lang_;
        ml.segs.push_back({MdSeg::CODE, code_buf, 0});
        out.push_back(ml);
    }

    return out;
}

// -------------------------------------------------------------------
// Syntax highlighting for code blocks
// -------------------------------------------------------------------

static bool is_word_char(char c) {
    return std::isalnum((unsigned char)c) || c == '_';
}

static bool is_syntax_keyword(const std::string& word, const std::string& lang) {
    static const std::unordered_map<std::string, std::vector<std::string>> keywords = {
        {"c", {"auto","break","case","char","const","continue","default","do","double",
               "else","enum","extern","float","for","goto","if","int","long","register",
               "return","short","signed","sizeof","static","struct","switch","typedef",
               "union","unsigned","void","volatile","while","include","define","ifdef",
               "endif","pragma","NULL","bool","true","false","nullptr"}},
        {"cpp", {"alignas","alignof","auto","bool","break","case","catch","char","class",
                 "const","constexpr","concept","continue","decltype","default","delete","do",
                 "double","else","enum","explicit","export","extern","false","float","for",
                 "friend","goto","if","include","inline","int","long","mutable","namespace",
                 "new","noexcept","nullptr","operator","override","private","protected",
                 "public","register","requires","return","short","signed","sizeof","static",
                 "static_cast","struct","switch","template","this","throw","true","try",
                 "typedef","typeid","typename","union","unsigned","using","virtual","void",
                 "volatile","while","define","ifdef","endif","pragma","include","NULL"}},
        {"csharp", {"abstract","as","async","await","base","bool","break","byte","case",
                    "catch","char","checked","class","const","continue","decimal","default",
                    "delegate","do","double","else","enum","event","explicit","extern",
                    "false","finally","fixed","float","for","foreach","goto","if","implicit",
                    "in","int","interface","internal","is","lock","long","namespace","new",
                    "null","object","operator","out","override","params","private","protected",
                    "public","readonly","ref","return","sbyte","sealed","short","sizeof",
                    "stackalloc","static","string","struct","switch","this","throw","true",
                    "try","typeof","uint","ulong","unchecked","unsafe","ushort","using",
                    "var","virtual","void","volatile","while","async","await","yield"}},
        {"python", {"False","None","True","and","as","assert","async","await","break","class",
                    "continue","def","del","elif","else","except","finally","for","from",
                    "global","if","import","in","is","lambda","match","nonlocal","not","or",
                    "pass","raise","return","try","while","with","yield","case"}},
        {"ruby", {"BEGIN","END","__ENCODING__","__END__","__FILE__","__LINE__","alias","and",
                  "begin","break","case","class","def","defined?","do","else","elsif","end",
                  "ensure","false","for","if","in","module","next","nil","not","or","redo",
                  "rescue","retry","return","self","super","then","true","undef","unless",
                  "until","when","while","yield"}},
        {"javascript", {"async","await","break","case","catch","class","const","continue",
                        "debugger","default","delete","do","else","export","extends","false",
                        "finally","for","function","if","import","in","instanceof","let",
                        "new","null","of","return","super","switch","this","throw","true",
                        "try","typeof","var","void","while","with","yield"}},
        {"typescript", {"abstract","as","async","await","break","case","catch","class","const",
                        "continue","debugger","declare","default","delete","do","else","enum",
                        "export","extends","false","finally","for","function","if","implements",
                        "import","in","instanceof","interface","is","keyof","let","module",
                        "namespace","never","new","null","of","readonly","return","satisfies",
                        "static","super","switch","this","throw","true","try","type","typeof",
                        "undefined","unique","unknown","var","void","while","with","yield"}},
        {"go", {"break","case","chan","const","continue","default","defer","else","fallthrough",
                "for","func","go","goto","if","import","interface","map","package","range",
                "return","select","struct","switch","type","var","true","false","nil"}},
        {"rust", {"as","async","await","break","const","continue","crate","dyn","else","enum",
                  "extern","false","fn","for","if","impl","in","let","loop","match","mod",
                  "move","mut","pub","ref","return","self","Self","static","struct","super",
                  "trait","true","type","unsafe","use","where","while","yield"}},
        {"java", {"abstract","assert","boolean","break","byte","case","catch","char","class",
                  "const","continue","default","do","double","else","enum","exports","extends",
                  "false","final","finally","float","for","goto","if","implements","import",
                  "instanceof","int","interface","long","module","native","new","null","open",
                  "opens","package","permits","private","protected","provides","public",
                  "record","requires","return","sealed","short","static","strictfp","super",
                  "switch","synchronized","this","throw","throws","to","transient","transitive",
                  "true","try","uses","var","void","volatile","while","with","yield"}},
        {"kotlin", {"abstract","actual","annotation","as","as?","break","by","catch","class",
                    "companion","const","constructor","continue","crossinline","data","delegate",
                    "do","dynamic","else","enum","expect","external","false","field","file",
                    "final","finally","for","fun","if","import","in","infix","init","inline",
                    "inner","interface","internal","is","it","lateinit","noinline","null",
                    "object","open","operator","out","override","package","param","private",
                    "property","protected","public","receiver","reified","return","sealed",
                    "set","super","suspend","tailrec","this","throw","true","try","typealias",
                    "typeof","val","var","vararg","when","while"}},
        {"swift", {"any","as","associativity","async","await","break","case","catch","class",
                   "continue","convenience","default","defer","deinit","didSet","do","dynamic",
                   "else","enum","extension","fallthrough","false","fileprivate","final","for",
                   "func","get","guard","if","import","in","indirect","infix","init","inout",
                   "internal","is","lazy","left","let","mutating","nil","none","nonmutating",
                   "open","operator","optional","override","package","postfix","precedence",
                   "prefix","private","protocol","public","repeat","required","rethrows",
                   "return","right","self","Self","set","some","static","struct","subscript",
                   "super","switch","throw","throws","true","try","typealias","unowned",
                   "var","weak","where","while","willSet"}},
        {"php", {"__CLASS__","__DIR__","__FILE__","__FUNCTION__","__LINE__","__METHOD__",
                 "__NAMESPACE__","__TRAIT__","abstract","and","array","as","break","callable",
                 "case","catch","class","clone","const","continue","declare","default","die",
                 "do","echo","else","elseif","empty","enddeclare","endfor","endforeach","endif",
                 "endswitch","endwhile","enum","eval","exit","extends","false","final",
                 "finally","fn","for","foreach","function","global","goto","if","implements",
                 "include","include_once","instanceof","insteadof","interface","isset","list",
                 "match","mixed","namespace","new","null","or","print","private","protected",
                 "public","readonly","require","require_once","return","static","switch",
                 "throw","trait","true","try","unset","use","var","while","xor","yield"}},
        {"lua", {"and","break","do","else","elseif","end","false","for","function","goto","if",
                 "in","local","nil","not","or","repeat","return","then","true","until","while"}},
        {"scala", {"abstract","case","catch","class","def","do","else","enum","export","extends",
                   "false","final","finally","for","given","if","implicit","import","lazy",
                   "match","new","null","object","override","package","private","protected",
                   "return","sealed","super","then","throw","trait","true","try","type","using",
                   "val","var","while","with","yield"}},
        {"perl", {"__DATA__","__END__","__FILE__","__LINE__","abs","alarm","and","caller",
                  "chdir","chmod","chomp","chop","close","cmp","connect","continue","cos",
                  "crypt","die","do","dump","each","else","elsif","endgrent","endhostent",
                  "endnetent","endprotoent","endpwent","endservent","eof","eq","eval","exec",
                  "exists","exit","exp","fcntl","fileno","flock","for","foreach","fork","format",
                  "formline","ge","getc","getgrgid","getgrnam","gethostbyaddr","gethostbyname",
                  "gethostent","getlogin","getnetbyaddr","getnetbyname","getnetent","getpeername",
                  "getpgrp","getppid","getpriority","getprotobyname","getprotobynumber",
                  "getprotoent","getpwent","getpwnam","getpwuid","getservbyname","getservbyport",
                  "getservent","getsockname","getsockopt","glob","gmtime","goto","grep","gt",
                  "hex","if","index","int","ioctl","join","keys","kill","last","lc","lcfirst",
                  "length","link","listen","local","localtime","lock","log","lstat","lt","map",
                  "mkdir","msgctl","msgget","msgrcv","msgsnd","my","ne","next","no","not","oct",
                  "open","opendir","or","our","pack","package","pipe","pop","pos","print",
                  "printf","prototype","push","quotemeta","rand","read","readdir","readline",
                  "readlink","readpipe","recv","redo","ref","rename","require","reset","return",
                  "reverse","rewinddir","rindex","rmdir","say","scalar","seek","seekdir","select",
                  "semctl","semget","semop","send","setgrent","sethostent","setnetent","setpgrp",
                  "setpriority","setprotoent","setpwent","setservent","setsockopt","shift",
                  "shmctl","shmget","shmread","shmwrite","shutdown","sin","sleep","socket",
                  "socketpair","sort","splice","split","sprintf","sqrt","srand","stat","state",
                  "study","substr","symlink","syscall","sysopen","sysread","sysseek","system",
                  "syswrite","tell","telldir","tie","tied","time","times","truncate","uc",
                  "ucfirst","umask","undef","unless","unlink","unpack","unshift","untie","until",
                  "use","utime","values","vec","wait","waitpid","wantarray","warn","while",
                  "write","xor","y"}},
        {"haskell", {"case","class","data","default","deriving","do","else","family","forall",
                     "foreign","hiding","if","import","in","infix","infixl","infixr","instance",
                     "let","module","newtype","of","open","pattern","qualified","role","safe",
                     "type","where","mdo","rec","proc","group","then"}},
        {"bash", {"if","then","else","elif","fi","for","while","do","done","case","esac",
                  "function","in","return","exit","break","continue","select","until","time",
                  "[[","]]","echo","export","local","source","unset","set","shift","trap",
                  "declare","typeset","read","printf","test","let","eval","exec"}},
    };
    auto it = keywords.find(lang);
    if (it == keywords.end()) return false;
    for (auto& kw : it->second) if (word == kw) return true;
    return false;
}

static bool is_syntax_builtin(const std::string& word, const std::string& lang) {
    static const std::unordered_map<std::string, std::vector<std::string>> builtins = {
        {"c", {"printf","scanf","fprintf","fscanf","sprintf","sscanf","malloc","calloc",
               "realloc","free","memcpy","memset","memmove","strlen","strcmp","strcpy",
               "strcat","strstr","strchr","fopen","fclose","fread","fwrite","fgets",
               "fputs","fgetc","fputc","fseek","ftell","rewind","feof","ferror","exit",
               "abort","assert","abs","sin","cos","tan","sqrt","pow","exp","log","rand",
               "srand","qsort","bsearch","atoi","atol","atof","offsetof"}},
        {"cpp", {"std","string","vector","map","set","unordered_map","unordered_set",
                 "shared_ptr","unique_ptr","weak_ptr","make_shared","make_unique",
                 "cout","cin","cerr","clog","endl","pair","tuple","optional","variant",
                 "any","function","bind","ref","cref","array","list","deque","queue",
                 "stack","priority_queue","multimap","multiset","bitset","valarray",
                 "complex","regex","smatch","sregex_iterator","thread","mutex","lock_guard",
                 "unique_lock","condition_variable","future","promise","async","packaged_task",
                 "chrono","duration","time_point","system_clock","steady_clock",
                 "high_resolution_clock","filesystem","path","directory_entry",
                 "iostream","fstream","sstream","stringstream","ostream","istream",
                 "ifstream","ofstream","numeric_limits","initializer_list",
                 "type_info","type_index","bad_cast","bad_alloc","exception"}},
        {"csharp", {"Console","String","Int32","Int64","Double","Single","Boolean","Byte",
                    "Char","Decimal","DateTime","TimeSpan","Guid","Task","Task<T>",
                    "List","Dictionary","HashSet","Queue","Stack","IEnumerable",
                    "IEnumerator","IDisposable","IQueryable","StringBuilder",
                    "Math","Convert","Array","Regex","File","Directory","Path",
                    "Stream","StreamReader","StreamWriter","HttpClient",
                    "Action","Func","Predicate","Tuple","ValueTuple","Nullable"}},
        {"python", {"print","range","len","int","str","float","list","dict","set","tuple",
                    "type","isinstance","enumerate","zip","map","filter","sorted","reversed",
                    "open","super","self","__init__","__str__","__repr__","__len__",
                    "__call__","__getitem__","__setitem__","__enter__","__exit__",
                    "__add__","__sub__","__mul__","__truediv__","__eq__","__ne__",
                    "__lt__","__le__","__gt__","__ge__","__hash__","__iter__",
                    "__next__","__contains__","__bool__","__del__","__delitem__",
                    "Any","Optional","List","Dict","Set","Tuple","Union","Callable",
                    "TypeVar","Generic","Protocol","Final","Literal","ClassVar"}},
        {"javascript", {"console","log","warn","error","dir","time","timeEnd",
                        "require","module","exports","__dirname","__filename",
                        "setTimeout","setInterval","clearTimeout","clearInterval",
                        "fetch","JSON","stringify","parse","Math","Date","RegExp",
                        "Error","TypeError","SyntaxError","ReferenceError",
                        "Promise","Map","Set","WeakMap","WeakSet","Symbol",
                        "Array","Object","String","Number","Boolean","Function",
                        "undefined","NaN","Infinity","isNaN","parseInt","parseFloat",
                        "encodeURI","decodeURI","encodeURIComponent","decodeURIComponent",
                        "localStorage","sessionStorage","window","document",
                        "Buffer","process","global","module","exports","require"}},
        {"typescript", {"console","log","warn","error","require","module","exports",
                        "fetch","JSON","Math","Date","RegExp","Error","Promise",
                        "Map","Set","Array","Object","String","Number","Boolean",
                        "undefined","NaN","any","never","unknown","string","number",
                        "boolean","void","null","undefined","symbol","bigint",
                        "Record","Partial","Required","Readonly","Pick","Omit",
                        "Extract","Exclude","NonNullable","ReturnType","InstanceType",
                        "Parameters","ConstructorParameters","Awaited",
                        "PromiseLike","ArrayLike","Iterable","IterableIterator"}},
        {"go", {"string","int","int8","int16","int32","int64","uint","uint8","uint16",
                "uint32","uint64","uintptr","byte","rune","float32","float64",
                "complex64","complex128","bool","error","nil","true","false",
                "make","len","cap","append","copy","delete","close","new","panic",
                "recover","print","println","fmt","Sprintf","Printf","Println",
                "Sscanf","Scanf","Scanln","Errorf","io","Reader","Writer",
                "Read","Write","ReadAll","Copy","Discard","NopCloser",
                "http","Get","Post","Handle","ListenAndServe","NewServeMux",
                "json","Marshal","Unmarshal","NewEncoder","NewDecoder",
                "os","Open","Create","Remove","Getenv","Setenv","Getwd","Chdir",
                "ioutil","ReadFile","WriteFile","ReadDir","TempFile","TempDir",
                "context","Background","TODO","WithCancel","WithTimeout","WithValue"}},
        {"rust", {"String","Vec","HashMap","HashSet","Box","Arc","Rc","Cell",
                  "RefCell","Mutex","RwLock","Option","Some","None","Result",
                  "Ok","Err","Iterator","IntoIterator","FromIterator",
                  "Clone","Copy","Debug","Display","Eq","PartialEq","Ord",
                  "PartialOrd","Hash","Default","Deref","Drop","From","Into",
                  "AsRef","AsMut","ToOwned","Borrow","BorrowMut",
                  "println","print","format","eprintln","eprint",
                  "vec","format_args","assert","assert_eq","assert_ne",
                  "panic","unreachable","unimplemented","todo","dbg",
                  "cfg","include_str","include_bytes","file","line","column"}},
        {"java", {"String","System","out","in","err","print","println","printf",
                  "Integer","Double","Float","Long","Short","Byte","Boolean",
                  "Character","Math","Object","Class","ArrayList","LinkedList",
                  "HashMap","HashSet","TreeMap","TreeSet","LinkedHashMap",
                  "Arrays","Collections","Comparator","Comparable","Iterator",
                  "Iterable","List","Set","Map","Queue","Deque","Stack",
                  "File","Path","Paths","Files","Stream","Collectors",
                  "Optional","OptionalInt","OptionalDouble","OptionalLong",
                  "Runnable","Callable","Thread","Executor","Executors",
                  "Future","CompletableFuture","Supplier","Consumer",
                  "Function","Predicate","BiFunction","UnaryOperator",
                  "IOException","RuntimeException","NullPointerException",
                  "IllegalArgumentException","IllegalStateException",
                  "StringBuilder","StringBuffer","Pattern","Matcher",
                  "Exception","Throwable","Error","Enum","Annotation"}},
        {"kotlin", {"println","print","readln","readLine","listOf","setOf","mapOf",
                    "mutableListOf","mutableSetOf","mutableMapOf","arrayOf",
                    "emptyList","emptySet","emptyMap","sequence","generateSequence",
                    "String","Int","Double","Float","Long","Short","Byte","Boolean",
                    "Char","Any","Unit","Nothing","List","MutableList","ArrayList",
                    "Set","MutableSet","HashSet","Map","MutableMap","HashMap",
                    "Pair","Triple","Sequence","Iterable","MutableIterable",
                    "Comparable","Comparator","CharSequence","Number",
                    "Collection","MutableCollection","Iterable","Iterator",
                    "Map","Entry","Set","List","MutableList","MutableSet",
                    "MutableMap","HashMap","LinkedHashMap","LinkedHashSet",
                    "Array","ByteArray","ShortArray","IntArray","LongArray",
                    "FloatArray","DoubleArray","CharArray","BooleanArray",
                    "run","let","apply","also","with","use","takeIf","takeUnless",
                    "repeat","check","checkNotNull","require","requireNotNull",
                    "TODO","error","assert"}},
        {"swift", {"print","debugPrint","dump","type","String","Int","Double","Float",
                   "Bool","Character","Array","Set","Dictionary","Optional",
                   "Any","AnyObject","Codable","Encodable","Decodable",
                   "Equatable","Hashable","Comparable","Identifiable",
                   "CustomStringConvertible","Error","Result","never",
                   "fatalError","precondition","preconditionFailure",
                   "assert","assertionFailure","abs","min","max","zip",
                   "stride","sequence","repeatElement","type","sizeof",
                   "UIView","UIViewController","UIColor","UIImage",
                   "NSObject","NSString","NSArray","NSDictionary",
                   "CGFloat","CGPoint","CGSize","CGRect","CGColorSpace"}},
        {"php", {"echo","print","die","exit","isset","empty","unset","eval",
                 "array","list","count","sizeof","in_array","array_push",
                 "array_pop","array_merge","array_keys","array_values",
                 "sort","rsort","asort","ksort","usort","uasort","uksort",
                 "implode","explode","substr","strpos","str_replace",
                 "trim","ltrim","rtrim","strlen","strtolower","strtoupper",
                 "ucfirst","lcfirst","ucwords","preg_match","preg_replace",
                 "preg_split","htmlspecialchars","htmlentities",
                 "json_encode","json_decode","serialize","unserialize",
                 "file_get_contents","file_put_contents","fopen","fclose",
                 "fgets","fputs","fwrite","feof","fread","fgetcsv",
                 "date","time","strtotime","mktime","DateTime",
                 "header","setcookie","session_start","session_destroy",
                 "mysqli_connect","mysqli_query","PDO","Exception",
                 "var_dump","print_r","error_reporting","ini_set",
                 "strval","intval","floatval","boolval"}},
        {"ruby", {"puts","print","p","pp","require","include","extend","attr_reader",
                  "attr_writer","attr_accessor","module_function",
                  "private","protected","public","public_send","send",
                  "raise","fail","catch","throw","block_given?",
                  "each","map","select","reject","reduce","inject",
                  "filter","find","all?","any?","none?","one?",
                  "count","size","length","empty?","include?",
                  "sort","sort_by","group_by","partition",
                  "new","initialize","super","yield",
                  "nil?","true?","false?","instance_of?","is_a?",
                  "respond_to?","method","class","superclass",
                  "Array","Hash","String","Integer","Float","Symbol",
                  "Range","Regexp","Proc","Lambda","Method","Binding",
                  "File","Dir","Pathname","IO","Errno",
                  "Time","Date","DateTime","JSON","YAML"}},
        {"lua", {"print","type","pairs","ipairs","next","select","tonumber","tostring",
                 "rawget","rawset","rawequal","setmetatable","getmetatable",
                 "require","dofile","loadfile","load","pcall","xpcall",
                 "unpack","table","concat","insert","remove","sort",
                 "string","sub","upper","lower","rep","reverse","format","match",
                 "gmatch","gsub","find","char","byte","len",
                 "math","abs","ceil","floor","max","min","pow","sqrt",
                 "sin","cos","tan","asin","acos","atan","exp","log",
                 "random","randomseed","huge","pi",
                 "io","open","close","read","write","lines","flush",
                 "os","clock","time","date","difftime","execute",
                 "coroutine","create","resume","yield","status","running",
                 "_G","_VERSION","assert","error","collectgarbage"}},
        {"scala", {"println","print","printf","readLine",
                   "String","Int","Double","Float","Long","Short","Byte",
                   "Boolean","Char","Unit","Nothing","Any","AnyVal","AnyRef",
                   "Option","Some","None","Either","Left","Right",
                   "Try","Success","Failure","Future","Promise",
                   "List","Set","Map","Seq","Array","Vector","Range",
                   "Iterator","Iterable","Traversable",
                   "Map","flatMap","filter","fold","reduce","foreach",
                   "collect","partition","groupBy","sortBy","sorted",
                   "zip","unzip","mkString","toString",
                   "Predef","Console","App","main","args",
                   "Nil","::","Stream","LazyList"}},
        {"perl", {"print","say","printf","sprintf","die","warn","exit",
                  "my","our","local","state","use","require","no",
                  "shift","unshift","push","pop","splice","split","join",
                  "map","grep","sort","reverse","keys","values","each",
                  "open","close","read","write","print","say","printf",
                  "<>","chomp","chop","defined","undef","length",
                  "substr","index","rindex","lc","uc","ucfirst","lcfirst",
                  "sin","cos","exp","log","sqrt","int","abs","rand",
                  "scalar","wantarray","ref","bless","tie","untie",
                  "die","eval","warn","caller","package",
                  "STDIN","STDOUT","STDERR","ARGV","ENV","SIG",
                  "BEGIN","CHECK","INIT","END","UNITCHECK"}},
        {"haskell", {"print","putStrLn","putStr","getLine","getChar","readFile",
                     "writeFile","appendFile","interact","readLn",
                     "map","filter","foldl","foldr","foldl1","foldr1",
                     "zipWith","scanl","scanr","iterate","repeat","cycle",
                     "take","drop","splitAt","takeWhile","dropWhile",
                     "elem","notElem","lookup","find","partition",
                     "head","tail","last","init","null","length",
                     "reverse","and","or","any","all","sum","product",
                     "maximum","minimum","concat","concatMap",
                     "show","read","reads","shows","readMaybe",
                     "fmap","pure","<*>",">>=",">>","return",
                     "curry","uncurry","flip","id","const",
                     "maybe","either","fromMaybe","maybeToList",
                     "IO","Maybe","Just","Nothing","Either","Left","Right",
                     "Ordering","EQ","LT","GT","Bool","True","False",
                     "Char","String","Int","Integer","Float","Double",
                     "Show","Read","Eq","Ord","Enum","Bounded",
                     "Num","Integral","Floating","Fractional","Real",
                     "Functor","Applicative","Monad","Foldable","Traversable",
                     "Monoid","Semigroup","Alternative","Category"}},
        {"bash", {"echo","printf","read","export","local","source","unset",
                  "set","shift","trap","declare","typeset","alias","unalias",
                  "let","test","exec","eval","exit","return","break","continue",
                  "." ,"true","false","cd","pwd","ls","mkdir","rmdir","rm",
                  "cp","mv","cat","less","more","head","tail","grep","sed",
                  "awk","cut","sort","uniq","wc","find","xargs","tee","tr",
                  "basename","dirname","realpath","readlink","which","type",
                  "sleep","wait","kill","ps","top","df","du","mount","umount",
                  "chmod","chown","ln","tar","gzip","gunzip","bzip2","xz",
                  "diff","patch","comm","cmp","seq","expr","bc","calc"}},
    };
    auto it = builtins.find(lang);
    if (it == builtins.end()) return false;
    for (auto& b : it->second) if (word == b) return true;
    return false;
}

std::vector<MdSeg> md_syntax_highlight(const std::string& code, const std::string& lang) {
    std::vector<MdSeg> segs;
    std::string lower_lang;
    for (char c : lang) lower_lang += (char)std::tolower((unsigned char)c);

    size_t i = 0;
    while (i < code.size()) {
        // Line comments //
        if ((lower_lang == "c" || lower_lang == "cpp" || lower_lang == "csharp" ||
             lower_lang == "java" || lower_lang == "javascript" || lower_lang == "typescript" ||
             lower_lang == "go" || lower_lang == "rust" || lower_lang == "kotlin" ||
             lower_lang == "swift" || lower_lang == "scala" || lower_lang == "php" ||
             lower_lang == "dart") &&
            i + 1 < code.size() && code[i] == '/' && code[i+1] == '/') {
            size_t end = code.find('\n', i);
            if (end == std::string::npos) end = code.size();
            segs.push_back({MdSeg::SYNTAX_COMMENT, code.substr(i, end - i), 0});
            i = end;
            continue;
        }
        // Block comments /* */
        if ((lower_lang == "c" || lower_lang == "cpp" || lower_lang == "csharp" ||
             lower_lang == "java" || lower_lang == "javascript" || lower_lang == "typescript" ||
             lower_lang == "go" || lower_lang == "rust" || lower_lang == "kotlin" ||
             lower_lang == "swift" || lower_lang == "scala" || lower_lang == "php") &&
            i + 1 < code.size() && code[i] == '/' && code[i+1] == '*') {
            size_t end = code.find("*/", i + 2);
            if (end == std::string::npos) end = code.size();
            else end += 2;
            segs.push_back({MdSeg::SYNTAX_COMMENT, code.substr(i, end - i), 0});
            i = end;
            continue;
        }
        // Lua/Haskell line comments --
        if ((lower_lang == "lua" || lower_lang == "haskell" || lower_lang == "sql") &&
            i + 1 < code.size() && code[i] == '-' && code[i+1] == '-') {
            size_t end = code.find('\n', i);
            if (end == std::string::npos) end = code.size();
            segs.push_back({MdSeg::SYNTAX_COMMENT, code.substr(i, end - i), 0});
            i = end;
            continue;
        }
        // Python/Ruby/Bash/PHP/Perl comments #
        if ((lower_lang == "python" || lower_lang == "ruby" || lower_lang == "bash" ||
             lower_lang == "yaml" || lower_lang == "r" || lower_lang == "perl" ||
             lower_lang == "php") && code[i] == '#') {
            size_t end = code.find('\n', i);
            if (end == std::string::npos) end = code.size();
            segs.push_back({MdSeg::SYNTAX_COMMENT, code.substr(i, end - i), 0});
            i = end;
            continue;
        }
        // Strings (double quoted)
        if (code[i] == '"') {
            size_t end = i + 1;
            while (end < code.size()) {
                if (code[end] == '\\') { end += 2; continue; }
                if (code[end] == '"') { end++; break; }
                end++;
            }
            segs.push_back({MdSeg::SYNTAX_STRING, code.substr(i, end - i), 0});
            i = end;
            continue;
        }
        // Strings (single quoted)
        if (code[i] == '\'') {
            size_t end = i + 1;
            while (end < code.size()) {
                if (code[end] == '\\') { end += 2; continue; }
                if (code[end] == '\'') { end++; break; }
                end++;
            }
            segs.push_back({MdSeg::SYNTAX_STRING, code.substr(i, end - i), 0});
            i = end;
            continue;
        }
        // Template literals (backtick) - JS/TS
        if (code[i] == '`' &&
            (lower_lang == "javascript" || lower_lang == "typescript")) {
            size_t end = i + 1;
            while (end < code.size()) {
                if (code[end] == '\\') { end += 2; continue; }
                if (code[end] == '`') { end++; break; }
                end++;
            }
            segs.push_back({MdSeg::SYNTAX_STRING, code.substr(i, end - i), 0});
            i = end;
            continue;
        }
        // Preprocessor directives (#include, #define, etc.)
        if ((lower_lang == "c" || lower_lang == "cpp") && code[i] == '#') {
            size_t end = code.find('\n', i);
            if (end == std::string::npos) end = code.size();
            segs.push_back({MdSeg::SYNTAX_PREPROC, code.substr(i, end - i), 0});
            i = end;
            continue;
        }
        // Numbers
        if (std::isdigit((unsigned char)code[i]) ||
            (code[i] == '.' && i + 1 < code.size() && std::isdigit((unsigned char)code[i+1]))) {
            size_t end = i;
            if (code[end] == '0' && end + 1 < code.size() &&
                (code[end+1] == 'x' || code[end+1] == 'X')) {
                end += 2;
                while (end < code.size() && std::isxdigit((unsigned char)code[end])) end++;
            } else {
                while (end < code.size() && (std::isdigit((unsigned char)code[end]) || code[end] == '.')) end++;
                if (end < code.size() && (code[end] == 'f' || code[end] == 'F' ||
                    code[end] == 'l' || code[end] == 'L' || code[end] == 'u' || code[end] == 'U')) end++;
            }
            segs.push_back({MdSeg::SYNTAX_NUMBER, code.substr(i, end - i), 0});
            i = end;
            continue;
        }
        // Decorators/annotations (@override, @staticmethod, @api, etc.)
        if (code[i] == '@') {
            size_t end = i + 1;
            while (end < code.size() && is_word_char(code[end])) end++;
            if (end > i + 1) {
                segs.push_back({MdSeg::SYNTAX_ATTRIBUTE, code.substr(i, end - i), 0});
                i = end;
                continue;
            }
        }
        // Words (potential keywords, functions)
        if (is_word_char(code[i])) {
            size_t end = i;
            while (end < code.size() && is_word_char(code[end])) end++;
            std::string word = code.substr(i, end - i);
            // Peek ahead for function call: word followed by '(' (skip whitespace)
            bool is_function_call = false;
            if (!is_syntax_keyword(word, lower_lang) && !is_syntax_builtin(word, lower_lang)) {
                size_t peek = end;
                while (peek < code.size() && (code[peek] == ' ' || code[peek] == '\t')) peek++;
                if (peek < code.size() && code[peek] == '(') {
                    is_function_call = true;
                }
            }
            if (is_function_call) {
                segs.push_back({MdSeg::SYNTAX_FUNCTION, word, 0});
            } else if (is_syntax_keyword(word, lower_lang)) {
                segs.push_back({MdSeg::SYNTAX_KEYWORD, word, 0});
            } else if (is_syntax_builtin(word, lower_lang)) {
                segs.push_back({MdSeg::SYNTAX_BUILTIN, word, 0});
            } else {
                segs.push_back({MdSeg::NORMAL, word, 0});
            }
            i = end;
            continue;
        }
        // Operators and punctuation
        if (code[i] == '+' || code[i] == '-' || code[i] == '*' || code[i] == '/' ||
            code[i] == '=' || code[i] == '<' || code[i] == '>' || code[i] == '!' ||
            code[i] == '&' || code[i] == '|' || code[i] == '^' || code[i] == '~' ||
            code[i] == '%' || code[i] == '?' || code[i] == ':' || code[i] == ';' ||
            code[i] == ',' || code[i] == '.' || code[i] == '(' || code[i] == ')' ||
            code[i] == '{' || code[i] == '}' || code[i] == '[' || code[i] == ']') {
            segs.push_back({MdSeg::SYNTAX_OPERATOR, std::string(1, code[i]), 0});
            i++;
            continue;
        }
        // Everything else
        {
            size_t end = i + 1;
            while (end < code.size() && !is_word_char(code[end]) &&
                   code[end] != '"' && code[end] != '\'' && code[end] != '`' &&
                   code[end] != '/' && code[end] != '#' &&
                   !std::isdigit((unsigned char)code[end]) &&
                   code[end] != '+' && code[end] != '-' && code[end] != '*' &&
                   code[end] != '=' && code[end] != '<' && code[end] != '>' &&
                   code[end] != '!' && code[end] != '&' && code[end] != '|' &&
                   code[end] != '^' && code[end] != '~' && code[end] != '%' &&
                   code[end] != '?' && code[end] != ':' && code[end] != ';' &&
                   code[end] != ',' && code[end] != '.' &&
                   code[end] != '(' && code[end] != ')' &&
                   code[end] != '{' && code[end] != '}' &&
                   code[end] != '[' && code[end] != ']') end++;
            segs.push_back({MdSeg::NORMAL, code.substr(i, end - i), 0});
            i = end;
        }
    }
    return segs;
}
