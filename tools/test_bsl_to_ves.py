#!/usr/bin/env python3
"""Unit tests for bsl_to_ves.translate (run: python tools/test_bsl_to_ves.py)."""
import sys
import os

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from bsl_to_ves import translate, transliterate, translate_identifier  # noqa: E402

fail = 0


def check(name, got, expected):
    global fail
    if got != expected:
        fail += 1
        sys.stderr.write("FAIL %s\n  got:      %r\n  expected: %r\n" % (name, got, expected))
    else:
        sys.stdout.write("ok   %s\n" % name)


def contains(name, got, needles, absent=()):
    global fail
    bad = [s for s in needles if s not in got]
    present = [s for s in absent if s in got]
    if bad or present:
        fail += 1
        sys.stderr.write("FAIL %s\n  missing: %s\n  unexpected: %s\n  got: %r\n"
                         % (name, bad, present, got))
    else:
        sys.stdout.write("ok   %s\n" % name)


# Keywords: procedure/if/endif
contains("proc_if",
         translate("Процедура Тест() Экспорт\n"
                   "  Если Истина Тогда\n"
                   "    Возврат;\n"
                   "  КонецЕсли;\n"
                   "КонецПроцедуры"),
         ["Procedure", "Public", "If", "True", "Then", "Return", "EndIf", "EndProcedure"],
         absent=["Процедура", "Если", "КонецЕсли"])

# Foreach collapse + In
contains("foreach",
         translate("Для Каждого Элемент Из Список Цикл\n"
                   "  Прервать;\n"
                   "КонецЦикла;"),
         ["Foreach", "In", "Do", "Break", "EndDo"],
         absent=["Для", "Каждого", "Из", "Цикл", "КонецЦикла"])

# For..To
contains("for_to",
         translate("Для Сч = 1 По 10 Цикл КонецЦикла;"),
         ["For", "To", "Do", "EndDo"],
         absent=["Для", "По", "Цикл"])

# Try/except
contains("try",
         translate("Попытка\n  А = 1;\nИсключение\n  Сообщить(ОписаниеОшибки());\nКонецПопытки;"),
         ["Try", "Except", "Message", "ErrorDescription", "Endtry"],
         absent=["Попытка", "Исключение", "Сообщить", "КонецПопытки"])

# Builtins + logical operators
contains("builtins",
         translate("Если Не ПустаяСтрока(С) И СтрДлина(С) > 0 Тогда Сообщить(ВРег(С)); КонецЕсли;"),
         ["If", "Not", "IsBlankString", "And", "StrLen", "Message", "Upper", "EndIf"],
         absent=["ПустаяСтрока", "СтрДлина", "ВРег", "Сообщить"])

# Strings and comments are preserved verbatim (Cyrillic inside untouched)
contains("string_preserved",
         translate('Сообщить("Привет, Если Мир"); // Если комментарий'),
         ['"Привет, Если Мир"', "// Если комментарий", "Message"],
         absent=['"Privet'])

# Doubled-quote string stays intact
contains("string_doubled",
         translate('А = "он сказал ""да""";'),
         ['"он сказал ""да"""'])

# Dictionary translation of compound identifiers (consistent parts)
check("dict_compound", translate_identifier("СуммаДокумента"), "SumDocument")   # Сумма+Документа(→документ)
check("dict_declension", translate_identifier("СправочникиТоваров"), "CatalogsProduct")  # товаров→товар
check("dict_abbrev", translate_identifier("СуммаНДС"), "SumVAT")
check("dict_verb", translate_identifier("ПолучитьСписок"), "GetList")
check("dict_latin_passthrough", translate_identifier("myVar"), "myVar")
# fallback: unknown word transliterated, known word translated (mixed)
check("dict_fallback", translate_identifier("ЖёлтыйДокумент"), "ZheltyyDocument")

contains("dict_ident_in_code",
         translate("Перем СуммаНДС; СуммаНДС = 0;"),
         ["Var", "SumVAT"],
         absent=["СуммаНДС", "Перем"])

# New + preproc region + directive commenting + dict-translated identifiers
contains("misc",
         translate("&НаКлиенте\n#Область Служебные\nОбъект = Новый Массив;\n#КонецОбласти"),
         ["// &НаКлиенте", "#Region", "New", "Array", "Object", "#EndRegion"],
         absent=["#Область", "Новый", "Массив"])

# --- Object method mapping (method position, after a dot) ------------------
check("method_add", translate("Массив.Добавить(Элемент);"), "Array.Add(Item);")
check("method_count", translate("Массив.Количество();"), "Array.Count();")
check("method_sort", translate('ТЗ.Сортировать("Цена");'), 'TZ.Sort("Цена");')
check("method_next", translate("Выборка.Следующий();"), "Selection.Next();")
check("method_execute", translate("Запрос.Выполнить();"), "Query.Execute();")
check("method_setparam",
      translate('Запрос.УстановитьПараметр("Дата", Дата);'),
      'Query.SetParameter("Дата", Date);')
check("method_clone", translate("Т.Скопировать();"), "T.Clone();")
check("method_write", translate("Объект.Записать();"), "Object.Write();")

# Compound TYPE names (outside method position)
check("type_valuetable", translate("Т = Новый ТаблицаЗначений;"), "T = New Table;")
check("type_query", translate("Зпр = Новый Запрос;"), "Zpr = New Query;")
check("type_spreadsheet",
      translate("Док = Новый ТабличныйДокумент;"), "Dok = New SpreadsheetDocument;")

# Method position does NOT get keyword/builtin/type mapping:
# ".Найти" is a method (Find), and a member ".Запрос" is not the type Query.
check("method_not_builtin", translate("Спр.Найти(Код);"), "Spr.Find(Code);")
check("member_not_type", translate("Стр.Количество();"), "Str.Count();")
# Global Найти (not after dot) still maps as builtin.
check("global_find_builtin",
      translate('Поз = Найти("абв", "б");'), 'Poz = Find("абв", "б");')

if fail:
    sys.stderr.write("\n%d test(s) FAILED\n" % fail)
    sys.exit(1)
sys.stdout.write("\nall bsl_to_ves tests passed\n")
