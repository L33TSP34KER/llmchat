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
                continue;
            } else {
                in_code_block = false;
                MdLine ml;
                ml.is_code_block = true;
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
            MdLine ml;
            ml.is_blockquote = true;
            ml.segs = parse_inline(content);
            out.push_back(ml);
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
                out.push_back(ml);
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
                out.push_back(ml);
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
        { MdLine ml; ml.segs.push_back({MdSeg::CODE, code_buf, 0}); out.push_back(ml); }
    }

    return out;
}
