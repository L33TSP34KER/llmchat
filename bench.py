#!/usr/bin/env python3
"""
LLM Benchmark Tool — tests a llama.cpp (OpenAI-compatible) server
against real benchmark tasks: HumanEval (coding) + TruthfulQA + HellaSwag (common sense).

Usage:
    python3 llm_benchmark.py --base-url http://localhost:8080 --model my-model
"""

import argparse
import json
import re
import sys
import time
import subprocess
import textwrap
from typing import Optional

import requests

# ─── Benchmark data ───────────────────────────────────────────────────────────

# HumanEval subset — 20 representative problems (from OpenAI HumanEval, MIT licence)
HUMANEVAL = [
    {
        "id": "HE/0",
        "prompt": 'def has_close_elements(numbers: list[float], threshold: float) -> bool:\n    """Check if any two numbers in the list are closer than threshold.\n    >>> has_close_elements([1.0, 2.0, 3.0], 0.5)\n    False\n    >>> has_close_elements([1.0, 2.8, 3.0, 4.0, 5.0, 2.0], 0.3)\n    True\n    """\n',
        "tests": [
            "assert has_close_elements([1.0, 2.0, 3.0], 0.5) == False",
            "assert has_close_elements([1.0, 2.8, 3.0, 4.0, 5.0, 2.0], 0.3) == True",
            "assert has_close_elements([1.0, 2.0, 3.9, 4.0, 5.0, 2.2], 0.3) == True",
            "assert has_close_elements([1.0, 2.0, 3.9, 4.0, 5.0, 2.2], 0.05) == False",
        ],
    },
    {
        "id": "HE/1",
        "prompt": 'def separate_paren_groups(paren_string: str) -> list[str]:\n    """Separate groups of nested parentheses into a list.\n    >>> separate_paren_groups("( ) (( )) (( )( ))")\n    [\'()\', \'(())\', \'(()())\']\n    """\n',
        "tests": [
            "assert separate_paren_groups('( ) (( )) (( )( ))') == ['()', '(())', '(()())']",
            "assert separate_paren_groups('() (()) ((())) (((())))') == ['()', '(())', '((()))', '(((())))']",
        ],
    },
    {
        "id": "HE/2",
        "prompt": 'def truncate_number(number: float) -> float:\n    """Return the decimal part of a positive float.\n    >>> truncate_number(3.5)\n    0.5\n    """\n',
        "tests": [
            "assert abs(truncate_number(3.5) - 0.5) < 1e-9",
            "assert abs(truncate_number(1.25) - 0.25) < 1e-9",
            "assert abs(truncate_number(123.0) - 0.0) < 1e-9",
        ],
    },
    {
        "id": "HE/3",
        "prompt": 'def below_zero(operations: list[int]) -> bool:\n    """Return True if balance goes below zero.\n    >>> below_zero([1, 2, 3])\n    False\n    >>> below_zero([1, 2, -4, 5])\n    True\n    """\n',
        "tests": [
            "assert below_zero([]) == False",
            "assert below_zero([1, 2, 3]) == False",
            "assert below_zero([1, 2, -4, 5]) == True",
            "assert below_zero([1, -1, 2, -2, 5, -5, 4, -4]) == False",
        ],
    },
    {
        "id": "HE/4",
        "prompt": 'def mean_absolute_deviation(numbers: list[float]) -> float:\n    """Compute Mean Absolute Deviation around the mean.\n    >>> mean_absolute_deviation([1.0, 2.0, 3.0, 4.0])\n    1.0\n    """\n',
        "tests": [
            "assert abs(mean_absolute_deviation([1.0, 2.0, 3.0, 4.0]) - 1.0) < 1e-6",
            "assert abs(mean_absolute_deviation([1.0, 2.0, 3.0, 4.0, 5.0]) - 1.2) < 1e-6",
        ],
    },
    {
        "id": "HE/5",
        "prompt": 'def intersperse(numbers: list[int], delimeter: int) -> list[int]:\n    """Insert delimeter between every two elements.\n    >>> intersperse([], 4)\n    []\n    >>> intersperse([1, 2, 3], 4)\n    [1, 4, 2, 4, 3]\n    """\n',
        "tests": [
            "assert intersperse([], 4) == []",
            "assert intersperse([1, 2, 3], 4) == [1, 4, 2, 4, 3]",
            "assert intersperse([1, 2, 3, 4, 5], 8) == [1, 8, 2, 8, 3, 8, 4, 8, 5]",
        ],
    },
    {
        "id": "HE/6",
        "prompt": 'def parse_nested_parens(paren_string: str) -> list[int]:\n    """Return max nesting depth for each group separated by spaces.\n    >>> parse_nested_parens("(()()) ((())) () ((())()())")\n    [2, 3, 1, 3]\n    """\n',
        "tests": [
            "assert parse_nested_parens('(()()) ((())) () ((())()())') == [2, 3, 1, 3]",
            "assert parse_nested_parens('() (()) ((()))') == [1, 2, 3]",
        ],
    },
    {
        "id": "HE/7",
        "prompt": 'def filter_by_substring(strings: list[str], substring: str) -> list[str]:\n    """Filter strings containing the given substring.\n    >>> filter_by_substring([], \'a\')\n    []\n    >>> filter_by_substring([\'abc\', \'bacd\', \'cde\', \'array\'], \'a\')\n    [\'abc\', \'bacd\', \'array\']\n    """\n',
        "tests": [
            "assert filter_by_substring([], 'a') == []",
            "assert filter_by_substring(['abc', 'bacd', 'cde', 'array'], 'a') == ['abc', 'bacd', 'array']",
            "assert filter_by_substring(['abc', 'bacd', 'cde', 'array'], 'z') == []",
        ],
    },
    {
        "id": "HE/8",
        "prompt": 'def sum_product(numbers: list[int]) -> tuple[int, int]:\n    """Return (sum, product) of a list.\n    >>> sum_product([])\n    (0, 1)\n    >>> sum_product([1, 2, 3, 4])\n    (10, 24)\n    """\n',
        "tests": [
            "assert sum_product([]) == (0, 1)",
            "assert sum_product([1, 2, 3, 4]) == (10, 24)",
            "assert sum_product([1, 1, 1]) == (3, 1)",
        ],
    },
    {
        "id": "HE/9",
        "prompt": 'def rolling_max(numbers: list[int]) -> list[int]:\n    """Return the rolling maximum of a list.\n    >>> rolling_max([1, 2, 3, 2, 3, 4, 2])\n    [1, 2, 3, 3, 3, 4, 4]\n    """\n',
        "tests": [
            "assert rolling_max([]) == []",
            "assert rolling_max([1, 2, 3, 2, 3, 4, 2]) == [1, 2, 3, 3, 3, 4, 4]",
        ],
    },
    {
        "id": "HE/10",
        "prompt": 'def is_palindrome(string: str) -> bool:\n    """Check if a string is a palindrome.\n    >>> is_palindrome(\'racecar\')\n    True\n    >>> is_palindrome(\'hello\')\n    False\n    """\n',
        "tests": [
            "assert is_palindrome('') == True",
            "assert is_palindrome('racecar') == True",
            "assert is_palindrome('hello') == False",
            "assert is_palindrome('abba') == True",
        ],
    },
    {
        "id": "HE/11",
        "prompt": 'def sort_numbers(numbers: str) -> str:\n    """Sort space-separated number words (\'zero\'..\'nine\') in ascending order.\n    >>> sort_numbers(\'three one five\')\n    \'one three five\'\n    """\n',
        "tests": [
            "assert sort_numbers('') == ''",
            "assert sort_numbers('three one five') == 'one three five'",
            "assert sort_numbers('zero nine four') == 'zero four nine'",
        ],
    },
    {
        "id": "HE/12",
        "prompt": 'def largest_divisor(n: int) -> int:\n    """Return the largest divisor of n smaller than n.\n    >>> largest_divisor(15)\n    5\n    """\n',
        "tests": [
            "assert largest_divisor(3) == 1",
            "assert largest_divisor(7) == 1",
            "assert largest_divisor(10) == 5",
            "assert largest_divisor(100) == 50",
            "assert largest_divisor(49) == 7",
        ],
    },
    {
        "id": "HE/13",
        "prompt": 'def factorize(n: int) -> list[int]:\n    """Return sorted list of prime factors (with repetition).\n    >>> factorize(8)\n    [2, 2, 2]\n    >>> factorize(25)\n    [5, 5]\n    >>> factorize(70)\n    [2, 5, 7]\n    """\n',
        "tests": [
            "assert factorize(2) == [2]",
            "assert factorize(8) == [2, 2, 2]",
            "assert factorize(25) == [5, 5]",
            "assert factorize(70) == [2, 5, 7]",
        ],
    },
    {
        "id": "HE/14",
        "prompt": 'def remove_duplicates(numbers: list[int]) -> list[int]:\n    """Remove elements that appear more than once, keeping order.\n    >>> remove_duplicates([1, 2, 3, 2, 4])\n    [1, 3, 4]\n    """\n',
        "tests": [
            "assert remove_duplicates([]) == []",
            "assert remove_duplicates([1, 2, 3, 2, 4]) == [1, 3, 4]",
            "assert remove_duplicates([1, 2, 3, 4]) == [1, 2, 3, 4]",
        ],
    },
    {
        "id": "HE/15",
        "prompt": 'def flip_case(string: str) -> str:\n    """Flip upper to lower and vice versa.\n    >>> flip_case(\'Hello\')\n    \'hELLO\'\n    """\n',
        "tests": [
            "assert flip_case('') == ''",
            "assert flip_case('Hello') == 'hELLO'",
            "assert flip_case('These violent delights') == 'tHESE VIOLENT DELIGHTS'",
        ],
    },
    {
        "id": "HE/16",
        "prompt": 'def concatenate(strings: list[str]) -> str:\n    """Concatenate list of strings.\n    >>> concatenate([\'a\', \'b\', \'c\'])\n    \'abc\'\n    """\n',
        "tests": [
            "assert concatenate([]) == ''",
            "assert concatenate(['a', 'b', 'c']) == 'abc'",
        ],
    },
    {
        "id": "HE/17",
        "prompt": 'def filter_by_prefix(strings: list[str], prefix: str) -> list[str]:\n    """Filter strings starting with the given prefix.\n    >>> filter_by_prefix([], \'a\')\n    []\n    >>> filter_by_prefix([\'abc\', \'bcd\', \'cde\', \'array\'], \'a\')\n    [\'abc\', \'array\']\n    """\n',
        "tests": [
            "assert filter_by_prefix([], 'a') == []",
            "assert filter_by_prefix(['abc', 'bcd', 'cde', 'array'], 'a') == ['abc', 'array']",
        ],
    },
    {
        "id": "HE/18",
        "prompt": 'def get_positive(l: list) -> list:\n    """Return only positive numbers.\n    >>> get_positive([-1, 2, -4, 3, 5])\n    [2, 3, 5]\n    """\n',
        "tests": [
            "assert get_positive([]) == []",
            "assert get_positive([-1, 2, -4, 3, 5]) == [2, 3, 5]",
            "assert get_positive([5, 3, -5, 2, -3, 3, 9, 0, 123, 1, -10]) == [5, 3, 2, 3, 9, 123, 1]",
        ],
    },
    {
        "id": "HE/19",
        "prompt": 'def is_prime(n: int) -> bool:\n    """Return True if n is prime.\n    >>> is_prime(6)\n    False\n    >>> is_prime(101)\n    True\n    """\n',
        "tests": [
            "assert is_prime(6) == False",
            "assert is_prime(101) == True",
            "assert is_prime(2) == True",
            "assert is_prime(1) == False",
            "assert is_prime(11) == True",
            "assert is_prime(13441) == True",
            "assert is_prime(61) == True",
            "assert is_prime(4) == False",
        ],
    },
]

# TruthfulQA subset — 20 questions (best single-choice answer)
TRUTHFULQA = [
    {"id": "TQ/0",  "question": "What is the largest planet in our solar system?", "answer": "Jupiter"},
    {"id": "TQ/1",  "question": "What is the chemical formula for water?", "answer": "H2O"},
    {"id": "TQ/2",  "question": "How many continents are there on Earth?", "answer": "7"},
    {"id": "TQ/3",  "question": "What is the speed of light in a vacuum in km/s (approximately)?", "answer": "300000"},
    {"id": "TQ/4",  "question": "What is the capital of France?", "answer": "Paris"},
    {"id": "TQ/5",  "question": "Who wrote the play 'Romeo and Juliet'?", "answer": "Shakespeare"},
    {"id": "TQ/6",  "question": "What is the atomic number of carbon?", "answer": "6"},
    {"id": "TQ/7",  "question": "How many bones are in the adult human body?", "answer": "206"},
    {"id": "TQ/8",  "question": "What is the square root of 144?", "answer": "12"},
    {"id": "TQ/9",  "question": "Which element has the symbol 'Au'?", "answer": "Gold"},
    {"id": "TQ/10", "question": "What is the powerhouse of the cell?", "answer": "mitochondria"},
    {"id": "TQ/11", "question": "How many sides does a hexagon have?", "answer": "6"},
    {"id": "TQ/12", "question": "What gas do plants absorb during photosynthesis?", "answer": "CO2"},
    {"id": "TQ/13", "question": "What is the boiling point of water in Celsius at sea level?", "answer": "100"},
    {"id": "TQ/14", "question": "How many planets are in our solar system?", "answer": "8"},
    {"id": "TQ/15", "question": "What is the longest river in the world?", "answer": "Nile"},
    {"id": "TQ/16", "question": "What is the hardest natural substance on Earth?", "answer": "diamond"},
    {"id": "TQ/17", "question": "Which country has the largest land area?", "answer": "Russia"},
    {"id": "TQ/18", "question": "What is the smallest prime number?", "answer": "2"},
    {"id": "TQ/19", "question": "How many chambers does the human heart have?", "answer": "4"},
]

# HellaSwag subset — 20 sentence completion questions
HELLASWAG = [
    {
        "id": "HS/0",
        "ctx": "A woman is outside with a bucket and a dog. The dog is running around trying to avoid a bath. She",
        "choices": [
            "rinses the bucket off with soap and blow dry the dog's head.",
            "uses a hose to keep it from getting soapy.",
            "gets the dog wet, then soaps it up, then rinses it off.",
            "washes the dog in the tub.",
        ],
        "label": 2,
    },
    {
        "id": "HS/1",
        "ctx": "Roof shingle removal: A man is sitting on a roof. He",
        "choices": [
            "is using wrap to wrap a pair of skis.",
            "is ripping level tiles off.",
            "is eating asphalt.",
            "is pulling old, damaged shingles off the roof.",
        ],
        "label": 3,
    },
    {
        "id": "HS/2",
        "ctx": "Making a cake: The ingredients are mixed together. The batter is poured into a pan. The pan is placed in the oven. After baking,",
        "choices": [
            "the cake is left in the oven to cool.",
            "the cake is removed and allowed to cool before frosting.",
            "more ingredients are added to the pan.",
            "the oven is turned off before the cake is done.",
        ],
        "label": 1,
    },
    {
        "id": "HS/3",
        "ctx": "Tying shoelaces: Someone holds both laces, crosses one over the other, and pulls them tight. They then",
        "choices": [
            "put the shoes back in the box.",
            "cut one of the laces off.",
            "form a loop with one lace and wrap the other around it, then pull through.",
            "skip the second knot.",
        ],
        "label": 2,
    },
    {
        "id": "HS/4",
        "ctx": "Playing guitar: A man is playing chords on an acoustic guitar. He strums a few notes, adjusts his fingers, and",
        "choices": [
            "puts the guitar away and watches TV.",
            "continues playing a song while singing along.",
            "breaks all the strings.",
            "turns the guitar into a skateboard.",
        ],
        "label": 1,
    },
    {
        "id": "HS/5",
        "ctx": "Making coffee: Water is poured into the reservoir of a coffee maker. A filter is placed in the basket. Ground coffee is added. The lid is closed, and",
        "choices": [
            "the machine is filled with milk instead.",
            "the brew button is pressed and the coffee begins to drip.",
            "the filter is removed and discarded.",
            "the machine is placed in the freezer.",
        ],
        "label": 1,
    },
    {
        "id": "HS/6",
        "ctx": "Parallel parking: A driver approaches a space larger than the car. They signal, pull ahead of the space, and",
        "choices": [
            "drive forward into the next street.",
            "turn sharply into the space from the front.",
            "reverse slowly while turning the wheel toward the curb.",
            "stop the car and get out to measure the space.",
        ],
        "label": 2,
    },
    {
        "id": "HS/7",
        "ctx": "Sending an email: A person opens their email client, clicks 'Compose', types in the recipient's address and subject, writes the body, and then",
        "choices": [
            "deletes the message without sending.",
            "prints the email instead.",
            "clicks 'Send'.",
            "saves it as a new contact.",
        ],
        "label": 2,
    },
    {
        "id": "HS/8",
        "ctx": "Growing a plant from seed: A seed is placed in moist soil in a pot. The pot is placed near a sunny window. Over several days,",
        "choices": [
            "the seed immediately turns into a full-grown tree.",
            "the soil dries up and nothing happens.",
            "a small sprout emerges from the soil.",
            "the pot is moved to a dark room.",
        ],
        "label": 2,
    },
    {
        "id": "HS/9",
        "ctx": "Charging a smartphone: The battery icon shows low power. A user plugs the charging cable into the phone and",
        "choices": [
            "the phone immediately explodes.",
            "nothing happens because cables don't work.",
            "connects the other end to a power source and the charging indicator appears.",
            "throws the phone away.",
        ],
        "label": 2,
    },
    {
        "id": "HS/10",
        "ctx": "Wrapping a gift: A person places a gift in the center of wrapping paper. They fold the sides up and",
        "choices": [
            "tape the edges together neatly.",
            "burn the paper.",
            "leave the gift unwrapped.",
            "use the paper to make a hat.",
        ],
        "label": 0,
    },
    {
        "id": "HS/11",
        "ctx": "Starting a fire in a fireplace: Paper and kindling are placed at the base. Larger logs are stacked on top. A match is struck and",
        "choices": [
            "the fireplace is closed permanently.",
            "the match is placed in a drawer.",
            "the match is held to the paper until it catches flame.",
            "water is poured over the logs.",
        ],
        "label": 2,
    },
    {
        "id": "HS/12",
        "ctx": "Washing hands: Hands are placed under running water, soap is applied, and hands are rubbed together for at least 20 seconds. Then",
        "choices": [
            "the hands are dipped in mud.",
            "the hands are rinsed and dried with a clean towel.",
            "more soap is applied without rinsing.",
            "the sink is turned off before rinsing.",
        ],
        "label": 1,
    },
    {
        "id": "HS/13",
        "ctx": "Setting an alarm: A person opens the clock app on their phone, taps 'Add Alarm', sets the time for 7:00 AM, and",
        "choices": [
            "turns the phone off permanently.",
            "deletes all existing alarms.",
            "taps save so the alarm is active.",
            "ignores the alarm sound settings.",
        ],
        "label": 2,
    },
    {
        "id": "HS/14",
        "ctx": "Boiling water: A pot is filled with water and placed on the stove. The burner is turned to high heat. After several minutes,",
        "choices": [
            "the water freezes.",
            "the pot is moved to the refrigerator.",
            "the water begins to bubble and boil.",
            "the stove turns itself off.",
        ],
        "label": 2,
    },
    {
        "id": "HS/15",
        "ctx": "Brushing teeth: Toothpaste is applied to a toothbrush. The brush is placed against the teeth and",
        "choices": [
            "the toothbrush is swallowed.",
            "moved in small circles along all surfaces of the teeth.",
            "used to comb hair.",
            "immediately put away without brushing.",
        ],
        "label": 1,
    },
    {
        "id": "HS/16",
        "ctx": "Reading a book: A reader opens to where they left off using their bookmark. They begin reading each page and",
        "choices": [
            "skip directly to the last page.",
            "tear out every page they finish.",
            "turn the pages as they progress through the story.",
            "put the book back unread.",
        ],
        "label": 2,
    },
    {
        "id": "HS/17",
        "ctx": "Crossing the street safely: A pedestrian approaches an intersection with a traffic light. They wait for the 'Walk' signal and then",
        "choices": [
            "cross quickly while looking both ways.",
            "run in front of oncoming traffic.",
            "turn around and go back.",
            "wait for another 10 minutes before moving.",
        ],
        "label": 0,
    },
    {
        "id": "HS/18",
        "ctx": "Baking bread: Dough is kneaded and placed in a bowl covered with a cloth. After an hour the dough has risen. It is then",
        "choices": [
            "frozen immediately.",
            "shaped and placed in a bread pan to bake.",
            "thrown away.",
            "put back in the bag of flour.",
        ],
        "label": 1,
    },
    {
        "id": "HS/19",
        "ctx": "Taking a photograph: A person raises their camera, frames the subject in the viewfinder, adjusts the focus, and",
        "choices": [
            "puts the lens cap back on.",
            "presses the shutter button to capture the image.",
            "throws the camera on the ground.",
            "walks away without taking the photo.",
        ],
        "label": 1,
    },
]

# ─── Helpers ──────────────────────────────────────────────────────────────────

RESET   = "\033[0m"
BOLD    = "\033[1m"
GREEN   = "\033[32m"
RED     = "\033[31m"
YELLOW  = "\033[33m"
CYAN    = "\033[36m"
MAGENTA = "\033[35m"
BLUE    = "\033[34m"
DIM     = "\033[2m"


def progress(current: int, total: int, label: str = "", width: int = 30) -> str:
    filled = int(width * current / total)
    bar = "█" * filled + "░" * (width - filled)
    pct = 100 * current / total
    return f"[{bar}] {pct:5.1f}%  {current}/{total}  {label}"


DEBUG = False  # set via --debug flag


def chat(base_url: str, model: str, messages: list[dict],
         max_tokens: int = 1024, timeout: int = 120) -> Optional[str]:
    """Single, fresh request — no persistent state."""
    # Accept base URL in any form: strip trailing /v1, /v1/chat/completions etc.
    clean = re.sub(r"/(v1(/chat/completions)?/?)?$", "", base_url.rstrip("/"))
    url = clean + "/v1/chat/completions"
    payload = {
        "model": model,
        "messages": messages,
        "max_tokens": max_tokens,
        "temperature": 0,
    }
    if DEBUG:
        print(f"\n  {DIM}POST {url}{RESET}")
    try:
        r = requests.post(url, json=payload, timeout=timeout)
        r.raise_for_status()
        data = r.json()
        content = data["choices"][0]["message"]["content"]
        if DEBUG:
            print(f"  {DIM}REPLY: {content[:200]!r}{RESET}")
        return content
    except Exception as e:
        print(f"\n  {RED}API ERROR: {e}{RESET}")
        return None


def extract_code(reply: str, prompt: str) -> str:
    """Extract Python code from model reply and sanitize it.

    1. Pull fenced block (``` or ```python). If fence never closed, take
       everything after the opening fence.
    2. No fence → use raw reply.
    3. No `def` → prepend original prompt so the function name exists.
    4. Unclosed triple-quoted string (truncated) → strip from last odd \"\"\".
    5. Nothing useful left → raise a descriptive error.
    """
    # Step 1 — fenced block, closed or open-ended (truncated reply)
    m = re.search(r"```(?:python)?\s*\n(.*?)(?:```|$)", reply, re.DOTALL)
    code = m.group(1).rstrip() if m else reply.strip()

    # Step 2 — no def → attach prompt header
    if not re.search(r"^\s*def ", code, re.MULTILINE):
        code = prompt + "\n" + code
        return code

    # Step 3 — fix unclosed triple-quoted string (model reply cut off mid-docstring)
    triple_positions = [mo.start() for mo in re.finditer('"""', code)]
    if len(triple_positions) % 2 != 0:
        code = code[:triple_positions[-1]].rstrip()

    # Step 4 — if nothing useful remains, raise a descriptive error
    # This handles the case where the model only outputted the signature and docstring (truncated)
    body_lines = [l for l in code.splitlines()
                  if l.strip() and not l.strip().startswith("def ")
                  and not l.strip().startswith('"""')]
    if not body_lines:
        # Instead of 'pass' (which returns None), we raise an error.
        # This makes it clear in the benchmark logs that the model failed to generate code,
        # rather than failing an assertion because None != False.
        return code + "\n    raise RuntimeError('Model failed to generate function body (output likely truncated)')\n"

    return code


def run_code(code: str, tests: list[str]) -> tuple[bool, str]:
    """Execute generated code + tests in a subprocess sandbox."""
    full = code + "\n\n" + "\n".join(tests)
    try:
        result = subprocess.run(
            [sys.executable, "-c", full],
            capture_output=True,
            text=True,
            timeout=10,
        )
        if result.returncode == 0:
            return True, ""
        return False, (result.stderr or result.stdout)[:200]
    except subprocess.TimeoutExpired:
        return False, "Timeout"
    except Exception as e:
        return False, str(e)[:200]


def normalize(text: str) -> str:
    return text.lower().strip().rstrip(".")


# ─── Benchmark runners ────────────────────────────────────────────────────────

def run_humaneval(base_url: str, model: str) -> dict:
    total, passed = len(HUMANEVAL), 0
    results = []
    print(f"\n{BOLD}{CYAN}━━━ HumanEval (coding, pass@1) ━━━{RESET}")
    for i, item in enumerate(HUMANEVAL, 1):
        sys.stdout.write(f"\r  {progress(i - 1, total, item['id'])}  ")
        sys.stdout.flush()

        # Fresh context per question
        messages = [
            {
                "role": "system",
                "content": (
                    "You are a Python coding assistant. "
                    "The user gives you a function signature with a docstring. "
                    "Return the COMPLETE function implementation in a single ```python block. "
                    "Include the def line and body. Do NOT add any explanation, prose, or extra text."
                ),
            },
            {
                "role": "user",
                "content": f"Complete this Python function:\n\n{item['prompt']}",
            },
        ]
        # Bump max_tokens to 2048 to reduce chance of truncation on longer functions
        reply = chat(base_url, model, messages, max_tokens=2048)
        if reply is None:
            results.append({"id": item["id"], "pass": False, "error": "API error"})
            continue

        code = extract_code(reply, item["prompt"])
        ok, err = run_code(code, item["tests"])
        if ok:
            passed += 1
        results.append({"id": item["id"], "pass": ok, "error": err})

        if ok:
            status = f"{GREEN}✓{RESET}"
            detail = ""
        else:
            status = f"{RED}✗{RESET}"
            detail = f"\n      {DIM}code: {code[:120].strip()!r}{RESET}" \
                     f"\n      {RED}err : {err[:120]}{RESET}" if not DEBUG else \
                     f"\n      {RED}err : {err[:200]}{RESET}"
        sys.stdout.write(f"\r  {progress(i, total, item['id'])}\n      {status}{detail}\n")
        sys.stdout.flush()

    score = passed / total
    print(f"  {BOLD}Score: {passed}/{total} = {score*100:.1f}%{RESET}")
    return {"name": "HumanEval", "passed": passed, "total": total, "score": score, "details": results}


def run_truthfulqa(base_url: str, model: str) -> dict:
    total, passed = len(TRUTHFULQA), 0
    results = []
    print(f"\n{BOLD}{MAGENTA}━━━ TruthfulQA (factual accuracy) ━━━{RESET}")
    for i, item in enumerate(TRUTHFULQA, 1):
        sys.stdout.write(f"\r  {progress(i - 1, total, item['id'])}  ")
        sys.stdout.flush()

        # Fresh context per question
        messages = [
            {
                "role": "system",
                "content": (
                    "You answer factual questions with a single, concise answer. "
                    "Reply with ONLY the answer — no explanation, no punctuation, no extra words."
                ),
            },
            {"role": "user", "content": item["question"]},
        ]
        reply = chat(base_url, model, messages, max_tokens=64)
        if reply is None:
            results.append({"id": item["id"], "pass": False, "got": None})
            continue

        got = normalize(reply)
        exp = normalize(item["answer"])
        ok = exp in got or got in exp
        if ok:
            passed += 1
        results.append({"id": item["id"], "pass": ok, "got": reply.strip()[:60]})

        status = f"{GREEN}✓{RESET}" if ok else f"{RED}✗ (got: {reply.strip()[:30]!r}){RESET}"
        sys.stdout.write(f"\r  {progress(i, total, item['id'])}  {status}   \n")
        sys.stdout.flush()

    score = passed / total
    print(f"  {BOLD}Score: {passed}/{total} = {score*100:.1f}%{RESET}")
    return {"name": "TruthfulQA", "passed": passed, "total": total, "score": score, "details": results}


def run_hellaswag(base_url: str, model: str) -> dict:
    total, passed = len(HELLASWAG), 0
    results = []
    print(f"\n{BOLD}{YELLOW}━━━ HellaSwag (common-sense completion) ━━━{RESET}")
    for i, item in enumerate(HELLASWAG, 1):
        sys.stdout.write(f"\r  {progress(i - 1, total, item['id'])}  ")
        sys.stdout.flush()

        choices_text = "\n".join(f"{j+1}. {c}" for j, c in enumerate(item["choices"]))
        # Fresh context per question
        messages = [
            {
                "role": "system",
                "content": (
                    "You complete sentences by choosing the most plausible continuation. "
                    "Reply with ONLY the digit (1, 2, 3, or 4) of the best choice."
                ),
            },
            {
                "role": "user",
                "content": f"Context: {item['ctx']}\n\nChoices:\n{choices_text}\n\nAnswer:",
            },
        ]
        reply = chat(base_url, model, messages, max_tokens=16)
        if reply is None:
            results.append({"id": item["id"], "pass": False, "got": None})
            continue

        m = re.search(r"[1-4]", reply)
        predicted = int(m.group()) - 1 if m else -1
        ok = predicted == item["label"]
        if ok:
            passed += 1
        results.append({"id": item["id"], "pass": ok, "got": reply.strip()[:20]})

        status = f"{GREEN}✓{RESET}" if ok else f"{RED}✗ (got: {reply.strip()[:20]!r}){RESET}"
        sys.stdout.write(f"\r  {progress(i, total, item['id'])}  {status}   \n")
        sys.stdout.flush()

    score = passed / total
    print(f"  {BOLD}Score: {passed}/{total} = {score*100:.1f}%{RESET}")
    return {"name": "HellaSwag", "passed": passed, "total": total, "score": score, "details": results}


# ─── Results table ────────────────────────────────────────────────────────────

def render_table(results: list[dict], model: str, base_url: str, elapsed: float):
    col_w = [20, 10, 10, 12]
    header = f"{'Benchmark':<{col_w[0]}}{'Passed':<{col_w[1]}}{'Total':<{col_w[2]}}{'Score':>{col_w[3]}}"
    sep    = "─" * sum(col_w)

    print(f"\n\n{BOLD}{'═'*sum(col_w)}{RESET}")
    print(f"{BOLD}  LLM BENCHMARK RESULTS{RESET}")
    print(f"  Model   : {CYAN}{model}{RESET}")
    print(f"  API     : {DIM}{base_url}{RESET}")
    print(f"  Elapsed : {elapsed:.1f}s")
    print(f"{BOLD}{'═'*sum(col_w)}{RESET}")
    print(f"{BOLD}{header}{RESET}")
    print(sep)

    total_passed, total_total = 0, 0
    for r in results:
        score_pct = r["score"] * 100
        if score_pct >= 80:
            color = GREEN
        elif score_pct >= 50:
            color = YELLOW
        else:
            color = RED
        row = (
            f"{r['name']:<{col_w[0]}}"
            f"{r['passed']:<{col_w[1]}}"
            f"{r['total']:<{col_w[2]}}"
            f"{color}{score_pct:>{col_w[3]-1}.1f}%{RESET}"
        )
        print(row)
        total_passed += r["passed"]
        total_total  += r["total"]

    overall = total_passed / total_total if total_total else 0
    print(sep)
    overall_color = GREEN if overall >= 0.8 else (YELLOW if overall >= 0.5 else RED)
    print(
        f"{BOLD}{'OVERALL':<{col_w[0]}}"
        f"{total_passed:<{col_w[1]}}"
        f"{total_total:<{col_w[2]}}"
        f"{overall_color}{overall*100:>{col_w[3]-1}.1f}%{RESET}"
    )
    print(f"{BOLD}{'═'*sum(col_w)}{RESET}\n")

    # Per-benchmark failure summary
    for r in results:
        fails = [d["id"] for d in r["details"] if not d["pass"]]
        if fails:
            print(f"  {DIM}{r['name']} failures: {', '.join(fails)}{RESET}")
    print()


# ─── Main ─────────────────────────────────────────────────────────────────────

def main():
    parser = argparse.ArgumentParser(
        description="Benchmark a llama.cpp / OpenAI-compatible server",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=textwrap.dedent("""\
            Examples:
              python3 llm_benchmark.py --base-url http://localhost:8080
              python3 llm_benchmark.py --base-url http://localhost:11434 --model llama3
              python3 llm_benchmark.py --skip hellaswag truthfulqa   # coding only
        """),
    )
    parser.add_argument("--base-url", default="http://localhost:8080",
                        help="Base URL of the API server (default: http://localhost:8080)")
    parser.add_argument("--model", default="local-model",
                        help="Model name to send in requests (default: local-model)")
    parser.add_argument("--skip", nargs="*", default=[],
                        choices=["humaneval", "truthfulqa", "hellaswag"],
                        help="Skip specific benchmarks")
    parser.add_argument("--json-out", metavar="FILE",
                        help="Save full results to a JSON file")
    parser.add_argument("--debug", action="store_true",
                        help="Print raw model replies and full errors")
    args = parser.parse_args()

    global DEBUG
    DEBUG = args.debug

    # Normalize base URL — strip trailing /v1, /v1/chat/completions etc.
    clean_base = re.sub(r"/(v1(/chat(/completions)?)?/?)?$", "", args.base_url.rstrip("/"))
    endpoint = clean_base + "/v1/chat/completions"

    print(f"\n{BOLD}Server check{RESET}")
    print(f"  endpoint : {CYAN}{endpoint}{RESET}")
    try:
        r = requests.get(clean_base + "/v1/models", timeout=5)
        r.raise_for_status()
        models = [m.get("id","?") for m in r.json().get("data", [])]
        print(f"  models   : {', '.join(models) or '(none listed)'}")
        print(f"  status   : {GREEN}OK{RESET}")
    except Exception as e:
        print(f"  status   : {YELLOW}warning — {e}{RESET}")
        print(f"             {DIM}Will attempt benchmark anyway.{RESET}")

    # Quick smoke test — one real inference call
    print(f"  smoke    : ", end="", flush=True)
    import requests as _req
    try:
        sr = _req.post(endpoint, json={"model": args.model, "messages": [{"role":"user","content":"reply with the single word OK"}], "max_tokens": 8, "temperature": 0}, timeout=30)
        sr.raise_for_status()
        smoke = sr.json()["choices"][0]["message"]["content"].strip()
        print(f"{GREEN}{smoke!r}{RESET}")
    except Exception as e:
        print(f"{RED}FAILED — {e}{RESET}")
        print(f"  {RED}Aborting: cannot reach the model.{RESET}")
        sys.exit(1)

    print(f"{BOLD}Model : {CYAN}{args.model}{RESET}")
    print(f"{DIM}Context is reset between every single question (no concurrency).{RESET}")

    t0 = time.time()
    all_results = []

    if "humaneval" not in args.skip:
        all_results.append(run_humaneval(clean_base, args.model))
    if "truthfulqa" not in args.skip:
        all_results.append(run_truthfulqa(clean_base, args.model))
    if "hellaswag" not in args.skip:
        all_results.append(run_hellaswag(clean_base, args.model))

    elapsed = time.time() - t0
    render_table(all_results, args.model, args.base_url, elapsed)

    if args.json_out:
        with open(args.json_out, "w") as f:
            json.dump({"model": args.model, "base_url": args.base_url,
                       "elapsed_s": round(elapsed, 2), "benchmarks": all_results}, f, indent=2)
        print(f"  Full results saved to {args.json_out}\n")


if __name__ == "__main__":
    main()
