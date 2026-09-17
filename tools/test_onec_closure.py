#!/usr/bin/env python3
"""Unit tests for onec_to_spec dependency-closure ref extractors (run: python tools/test_onec_closure.py).

Pure-python: exercises the reference-mapping helpers on inline strings — no 1C dump needed. Guards the
manager-collection / type-cast / common-module recognition that the --closure walk depends on.
"""
import sys
import os

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import onec_to_spec as o  # noqa: E402

fail = 0


def eq(name, got, expected):
    global fail
    if got != expected:
        fail += 1
        sys.stderr.write("FAIL %s\n  got:      %r\n  expected: %r\n" % (name, got, expected))
    else:
        sys.stdout.write("ok   %s\n" % name)


def code_refs(src, common=frozenset()):
    return set(o._iter_code_refs(src, common))


# Manager collections in code -> (kind, name).
eq("mgr_catalog", code_refs("x = Справочники.Валюты.НайтиПоКоду(1);"), {("Catalog", "Валюты")})
eq("mgr_document", code_refs("Документы.РеализацияТоваров.СоздатьДокумент();"), {("Document", "РеализацияТоваров")})
eq("mgr_enum", code_refs("Если Х = Перечисления.СтавкиНДС.НДС18 Тогда"), {("Enum", "СтавкиНДС")})
eq("mgr_inforeg", code_refs("РегистрыСведений.КурсыВалют.СоздатьНаборЗаписей();"),
   {("InformationRegister", "КурсыВалют")})
eq("mgr_accumreg", code_refs("РегистрыНакопления.Товары.Остатки();"), {("AccumulationRegister", "Товары")})

# Type-cast / ref prefixes in code (Тип("СправочникСсылка.X")).
eq("cast_ref", code_refs('Т = Тип("СправочникСсылка.Контрагенты");'), {("Catalog", "Контрагенты")})
eq("cast_docobj", code_refs('Если ТипЗнч(О) = Тип("ДокументОбъект.Заказ") Тогда'), {("Document", "Заказ")})
eq("cast_english_ref", code_refs('Т = Type("CatalogRef.Товары");'), {("Catalog", "Товары")})

# Qualified common-module call — recognised only when the head is a known common-module name.
eq("common_qualified_known",
   code_refs("ОбщегоНазначения.ЗначениеРеквизитаОбъекта(С);", common={"ОбщегоНазначения"}),
   {("CommonModule", "ОбщегоНазначения")})
eq("common_qualified_unknown",
   code_refs("НекийМодуль.Метод();", common=frozenset()),
   set())

# A bare global call is NOT a manager reference — it carries no qualifier, so nothing is pulled here
# (bare-call providers are covered by the always-include-global-common-modules pass, not code scan).
eq("bare_call_no_ref", code_refs("Если ЗначениеНеЗаполнено(Х) Тогда"), set())

# Ordinary member access on a local var must not be mistaken for a manager reference.
eq("member_access_ignored", code_refs("Стр = Объект.Наименование;"), set())

# Type refs from a parsed <Type> element tree.
import xml.etree.ElementTree as ET  # noqa: E402

V8 = o.V8[1:-1]  # strip the {...} braces for the ns map


def type_refs(*type_texts):
    root = ET.Element("Attribute")
    props = ET.SubElement(root, o.MD + "Properties")
    typ = ET.SubElement(props, o.MD + "Type")
    for t in type_texts:
        e = ET.SubElement(typ, o.V8 + "Type")
        e.text = t
    return set(o._iter_type_refs(root))


eq("type_ref_catalog", type_refs("cfg:CatalogRef.Номенклатура"), {("Catalog", "Номенклатура")})
eq("type_ref_composite",
   type_refs("cfg:CatalogRef.Контрагенты", "cfg:DocumentRef.Счет"),
   {("Catalog", "Контрагенты"), ("Document", "Счет")})
eq("type_ref_primitive_ignored", type_refs("xs:string"), set())

if fail:
    sys.stderr.write("\n%d FAILED\n" % fail)
    sys.exit(1)
sys.stdout.write("\nall closure ref-extractor tests passed\n")
