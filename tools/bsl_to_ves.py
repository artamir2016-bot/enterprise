#!/usr/bin/env python3
"""OES — translate 1C BSL module code into the OES VES dialect.

Lexer-based translation (MVP): 1C Russian keywords -> VES English keywords,
a curated set of 1C global functions -> OES built-ins, and Cyrillic identifiers
-> Latin transliteration. Strings, comments and date-literals are preserved
verbatim. 1C type/method library (Структура, ТаблицаЗначений, Справочники.*,
object methods) is NOT mapped — those identifiers are transliterated, so output
is dialect-correct but not guaranteed to execute.

Standalone:
    python tools/bsl_to_ves.py input.bsl > output.ves
Library:
    from bsl_to_ves import translate
    ves = translate(bsl_text)
"""
import re
import sys
import os
import json
from collections import Counter

# --- 1C keyword -> VES keyword (lower-cased 1C key) ------------------------
# Multi-word "Для Каждого" is handled before single-word mapping.
KEYWORDS = {
    "если": "If", "тогда": "Then", "иначеесли": "Elseif", "иначе": "Else",
    "конецесли": "EndIf",
    "для": "For", "по": "To", "каждого": "Foreach", "из": "In",
    "пока": "While", "цикл": "Do", "конеццикла": "EndDo",
    "процедура": "Procedure", "конецпроцедуры": "EndProcedure",
    "функция": "Function", "конецфункции": "EndFunction",
    "возврат": "Return", "прервать": "Break", "продолжить": "Continue",
    "перем": "Var", "знач": "Val", "экспорт": "Public",
    "попытка": "Try", "исключение": "Except", "конецпопытки": "Endtry",
    "вызватьисключение": "Raise",
    "новый": "New",
    "и": "And", "или": "Or", "не": "Not",
    "истина": "True", "ложь": "False", "неопределено": "Undefined",
    "перейти": "GoTo",
}

# --- 1C global function -> OES built-in (lower-cased 1C name) ---------------
BUILTINS = {
    "сообщить": "Message",
    "строка": "String", "число": "Number", "дата": "Date", "булево": "Boolean",
    "тип": "Type", "типзнч": "TypeOf",
    "стрдлина": "StrLen", "пустаястрока": "IsBlankString",
    "сокрлп": "TrimAll", "сокрл": "TrimL", "сокрп": "TrimR",
    "лев": "Left", "прав": "Right", "сред": "Mid",
    "найти": "Find", "стрзаменить": "StrReplace",
    "стрчислострок": "StrLineCount", "стрполучитьстроку": "StrGetLine",
    "стрколичествострок": "StrLineCount",
    "врег": "Upper", "нрег": "Lower",
    "символ": "Chr", "кодсимвола": "Asc",
    "окр": "Round", "цел": "Int", "макс": "Max", "мин": "Min",
    "лог10": "Log10", "лн": "Ln", "sqrt": "Sqrt",
    "текущаядата": "CurrentDate", "рабочаядата": "WorkingDate",
    "добавитьмесяц": "AddMonth",
    "год": "GetYear", "месяц": "GetMonth", "день": "GetDay",
    "час": "GetHour", "минута": "GetMinute", "секунда": "GetSecond",
    "неделягода": "GetWeekOfYear", "деньгода": "GetDayOfYear",
    "деньнедели": "GetDayOfWeek",
    "началомесяца": "BegOfMonth", "конецмесяца": "EndOfMonth",
    "началоквартала": "BegOfQuart", "конецквартала": "EndOfQuart",
    "началогода": "BegOfYear", "конецгода": "EndOfYear",
    "началонедели": "BegOfWeek", "конецнедели": "EndOfWeek",
    "началодня": "BegOfDay", "конецдня": "EndOfDay",
    "формат": "Format", "вычислить": "Evaluate", "выполнить": "Execute",
    "значениезаполнено": "ValueIsFilled",
    "описаниеошибки": "ErrorDescription",
    "вопрос": "Question", "предупреждение": "Alert",
    "очиститьсообщения": "ClearMessages",
    "имякомпьютера": "ComputerName", "имяпользователя": "UserName",
    "начатьтранзакцию": "BeginTransaction",
    "зафиксироватьтранзакцию": "CommitTransaction",
    "отменитьтранзакцию": "RollBackTransaction",
}

# --- Cyrillic -> Latin transliteration (GOST-ish, deterministic) -----------
TRANSLIT = {
    "а": "a", "б": "b", "в": "v", "г": "g", "д": "d", "е": "e", "ё": "e",
    "ж": "zh", "з": "z", "и": "i", "й": "y", "к": "k", "л": "l", "м": "m",
    "н": "n", "о": "o", "п": "p", "р": "r", "с": "s", "т": "t", "у": "u",
    "ф": "f", "х": "h", "ц": "ts", "ч": "ch", "ш": "sh", "щ": "sch",
    "ъ": "", "ы": "y", "ь": "", "э": "e", "ю": "yu", "я": "ya",
}


def _translit_char(ch):
    low = ch.lower()
    if low not in TRANSLIT:
        return ch  # non-cyrillic passes through
    rep = TRANSLIT[low]
    if not rep:
        return ""
    # Preserve case: uppercase source -> capitalize replacement.
    if ch.isupper():
        return rep[0].upper() + rep[1:]
    return rep


def transliterate(word):
    return "".join(_translit_char(c) for c in word)


# --- Dictionary-based identifier translation -------------------------------
# A compound 1C identifier (PascalCase of Russian words, e.g. СуммаДокумента)
# is split into parts, each part translated via a SHARED dictionary so the same
# part maps to the same English word in every module. Unknown parts fall back to
# transliteration and are recorded so the dictionary can be grown.

_DICT_PATH = os.path.join(os.path.dirname(os.path.abspath(__file__)), "ru_en_dict.json")

_HAS_CYR = re.compile(r"[А-Яа-яЁё]")
_SPLIT_RE = re.compile(
    r"[A-ZА-ЯЁ]+(?=[A-ZА-ЯЁ][a-zа-яё])"  # caps run before Cap+lower  (НДСДокумент)
    r"|[A-ZА-ЯЁ]?[a-zа-яё]+"             # Word / word
    r"|[A-ZА-ЯЁ]+"                       # trailing CAPS  (…НДС)
    r"|\d+"                              # digits
)


def _load_dict(path=_DICT_PATH):
    """Load the shared dictionary, splitting entries into single-part words and
    multi-part PHRASES. A key that is itself a compound identifier (e.g.
    "ПриСозданииНаСервере") becomes a PHRASE: its run of parts is matched as one
    unit inside any compound name and replaced by the analog, so every name that
    contains it translates consistently (the OES standard-event analog, not a
    part-by-part transliteration)."""
    try:
        with open(path, "r", encoding="utf-8") as f:
            raw = json.load(f)
    except OSError:
        return {}, {}
    words, phrases = {}, {}
    for k, v in raw.items():
        if k.startswith("_"):
            continue
        parts = _SPLIT_RE.findall(k)
        if len(parts) > 1:
            phrases[tuple(p.lower() for p in parts)] = v
        else:
            words[k.lower()] = v
    return words, phrases

DICT, PHRASES = _load_dict()
_PHRASE_MAX = max((len(k) for k in PHRASES), default=0)


def _load_map(fname):
    path = os.path.join(os.path.dirname(os.path.abspath(__file__)), fname)
    try:
        with open(path, "r", encoding="utf-8") as f:
            raw = json.load(f)
    except OSError:
        return {}
    return {k.lower(): v for k, v in raw.items() if not k.startswith("_")}


# 1C object method name -> OES method (applied in method position, after a dot).
METHODS = _load_map("onec_methods.json")
# 1C compound type name -> OES value-type name (applied outside method position).
TYPES = _load_map("onec_types.json")

# Missing (untranslated) word-parts, for the extend-the-dictionary report.
MISSING = Counter()

# Common declension / plural / adjective endings, longest first. Stripped only
# when the resulting stem is itself in the dictionary (so a wrong strip fails
# safely and the part transliterates instead).
_ENDINGS = ["ами", "ями", "ому", "ему", "ыми", "ими", "ого", "его",
            "ой", "ый", "ий", "ая", "яя", "ое", "ее", "ые", "ие",
            "ов", "ев", "ей", "ам", "ям", "ах", "ях", "ом", "ем",
            "у", "ю", "а", "я", "ы", "и", "е", "о"]

def _cap(word):
    return word[0].upper() + word[1:] if word else word


def _translate_part(part):
    low = part.lower()
    hit = DICT.get(low)
    if hit is not None:
        return hit
    if _HAS_CYR.search(part):
        for e in _ENDINGS:
            if low.endswith(e) and len(low) - len(e) >= 3:
                stem = DICT.get(low[: -len(e)])
                if stem is not None:
                    return stem
        MISSING[low] += 1
        return transliterate(part)
    return part  # already Latin — leave as-is


def translate_identifier(name):
    """Translate a compound Cyrillic identifier via the shared dictionary.

    A multi-word PHRASE (e.g. ПриСозданииНаСервере -> OnOpen) is matched first,
    greedily and longest-first, so it is replaced as one unit wherever it occurs;
    remaining parts translate individually."""
    parts = _SPLIT_RE.findall(name)
    if not parts:
        return transliterate(name)
    lowered = [p.lower() for p in parts]
    words = []
    i, n = 0, len(parts)
    while i < n:
        hit = None
        for plen in range(min(_PHRASE_MAX, n - i), 1, -1):   # longest phrase first
            repl = PHRASES.get(tuple(lowered[i:i + plen]))
            if repl is not None:
                hit = (repl, plen)
                break
        if hit is not None:
            words.append(hit[0])
            i += hit[1]
        else:
            words.append(_translate_part(parts[i]))
            i += 1
    result = "".join(_cap(w) for w in words)
    if name[:1].islower() and result:  # preserve camelCase locals
        result = result[0].lower() + result[1:]
    return result


def missing_report():
    """Return the sorted list of (word, count) parts that had no translation."""
    return MISSING.most_common()


# Token pattern: identifier (letters/digits/_ incl. cyrillic), preprocessor/
# directive word, number, string, date-literal, line-comment, or single char.
_IDENT = r"[A-Za-z_Ѐ-ӿ][A-Za-z0-9_Ѐ-ӿ]*"
_TOKEN_RE = re.compile(
    r"""
      (?P<comment>//[^\n]*)
    | (?P<string>"(?:[^"]|"")*")
    | (?P<date>'[^']*')
    | (?P<directive>&[A-Za-zЀ-ӿ]+)
    | (?P<preproc>\#[A-Za-zЀ-ӿ]+)
    | (?P<ident>""" + _IDENT + r""")
    | (?P<number>\d+(?:\.\d+)?)
    | (?P<ws>[ \t\r\n]+)
    | (?P<other>.)
    """,
    re.VERBOSE,
)

PREPROC = {
    "область": "#Region",
    "конецобласти": "#EndRegion",
}


def translate(text):
    """Translate BSL source text to VES. Returns the translated text."""
    # Tokenize into (kind, value) preserving order & whitespace.
    toks = []
    for m in _TOKEN_RE.finditer(text):
        toks.append((m.lastgroup, m.group()))

    # Map identifier tokens; collapse "Для Каждого" -> "Foreach".
    out = []
    i = 0
    n = len(toks)
    # index of identifier tokens only, for the "Для Каждого" lookahead
    def next_ident_index(start):
        j = start
        while j < n and toks[j][0] == "ws":
            j += 1
        return j if j < n and toks[j][0] == "ident" else -1

    after_dot = False  # True when the previous significant token was "."
    while i < n:
        kind, val = toks[i]

        if kind == "ws" or kind == "comment":
            out.append(val)
            i += 1
            continue  # whitespace/comment do not change method position

        if kind in ("string", "date", "number"):
            out.append(val)
            after_dot = False
            i += 1
            continue

        if kind == "other":
            out.append(val)
            after_dot = (val == ".")  # a member access follows
            i += 1
            continue

        if kind == "directive":
            out.append("// " + val)  # &НаКлиенте — no OES equivalent
            after_dot = False
            i += 1
            continue

        if kind == "preproc":
            key = val[1:].lower()
            if key in PREPROC:
                out.append(PREPROC[key])
            elif key in ("если", "иначеесли", "иначе", "конецесли"):
                out.append("// " + val)  # 1C conditional compilation ≠ OES #Ifdef
            else:
                out.append(val)
            after_dot = False
            i += 1
            continue

        # ident
        low = val.lower()

        if after_dot:
            # Member position: object method / property. Method map first,
            # then dictionary translation (never keyword/builtin/type here).
            m = METHODS.get(low)
            if m is not None:
                out.append(m)
            elif _HAS_CYR.search(val):
                out.append(translate_identifier(val))
            else:
                out.append(val)
            after_dot = False
            i += 1
            continue

        # "Для Каждого" -> "Foreach" (drop "Для", emit Foreach for Каждого).
        if low == "для":
            j = next_ident_index(i + 1)
            if j != -1 and toks[j][1].lower() == "каждого":
                out.append("Foreach")
                i = j + 1
                continue
            out.append("For")
            i += 1
            continue

        if low in KEYWORDS:
            out.append(KEYWORDS[low])
        elif low in BUILTINS:
            out.append(BUILTINS[low])
        elif low in TYPES:
            out.append(TYPES[low])
        elif _HAS_CYR.search(val):
            out.append(translate_identifier(val))
        else:
            out.append(val)  # already-Latin identifier — leave unchanged
        after_dot = False
        i += 1

    return "".join(out)


def main():
    if len(sys.argv) < 2:
        sys.stderr.write("usage: bsl_to_ves.py <input.bsl>\n")
        return 2
    with open(sys.argv[1], "r", encoding="utf-8-sig") as f:
        src = f.read()
    sys.stdout.write(translate(src))
    return 0


if __name__ == "__main__":
    sys.exit(main())
