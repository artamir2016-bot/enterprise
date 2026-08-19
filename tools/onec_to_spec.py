#!/usr/bin/env python3
"""OES — convert a 1C configuration file-dump into a friendly-JSON spec.

External, extensible importer front-end: reads a 1C "выгрузка в файлы" dump
(Configuration.xml + per-object XML + Ext/*.bsl modules) file-by-file and emits
the friendly JSON consumed by oes_config_gen, which builds the .mcf.

    python tools/onec_to_spec.py <dump_dir> <out.json> [--only Catalogs,Enums,...] [--limit N]
    build/.../oes_config_gen out.json out.mcf

MVP metatypes: Catalogs, Documents (attributes incl. references + tabular
sections), Enums (values), Constants (type), InformationRegisters +
AccumulationRegisters (dimensions/resources/attributes), CommonModules (BSL
verbatim), plus object/manager module text. 1C BSL is copied verbatim (not
translated). Types outside the MVP degrade to String (counted in the report).

The parser reads each object XML wholly (they are small); the dump is walked
per-file so peak memory tracks the configuration size, not one giant document.
"""
import argparse
import os
import sys
import json
import xml.etree.ElementTree as ET
from collections import Counter

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bsl_to_ves  # noqa: E402

# Toggled by --no-translate-bsl; when True, BSL module code is translated to VES.
TRANSLATE_BSL = True


def maybe_translate(code):
    if code and TRANSLATE_BSL:
        return bsl_to_ves.translate(code)
    return code

MD = "{http://v8.1c.ru/8.3/MDClasses}"
V8 = "{http://v8.1c.ru/8.1/data/core}"

# 1C reference type prefix -> OES friendly ref namespace (MVP targets only).
REF_PREFIX = {
    "CatalogRef": "Catalog",
    "DocumentRef": "Document",
    "EnumRef": "Enum",
}

report = Counter()


def _txt(el):
    return el.text if el is not None and el.text is not None else ""


def _local(tag):
    return tag.rsplit("}", 1)[-1]


def read_file(path):
    try:
        with open(path, "r", encoding="utf-8-sig") as f:
            return f.read()
    except OSError:
        return ""


def map_type(type_el):
    """Map a 1C <Type> element to a friendly-JSON type dict."""
    if type_el is None:
        return {"type": "String", "length": 0}

    v8_types = [ _txt(t).strip() for t in type_el.findall(V8 + "Type") ]
    v8_types = [t for t in v8_types if t]

    # Reference / composite reference?
    refs = []
    non_ref = []
    for t in v8_types:
        # e.g. "cfg:CatalogRef.Товары" -> ("CatalogRef", "Товары")
        name = t.split(":", 1)[-1]
        if "." in name:
            kind, obj = name.split(".", 1)
            if kind in REF_PREFIX:
                refs.append("%s.%s" % (REF_PREFIX[kind], obj))
                continue
        non_ref.append(t)

    if refs and not non_ref:
        report["ref"] += 1
        return {"type": "ref", "refs": refs}
    if refs and non_ref:
        # mixed composite (ref + primitive / out-of-MVP) — keep the refs,
        # drop the rest (structural import).
        report["ref_mixed"] += 1
        return {"type": "ref", "refs": refs}

    # Primitive
    prim = v8_types[0] if v8_types else "xs:string"
    if prim == "xs:string":
        q = type_el.find(V8 + "StringQualifiers")
        length = 0
        if q is not None:
            length = int(_txt(q.find(V8 + "Length")) or "0")
        report["String"] += 1
        return {"type": "String", "length": length}
    if prim == "xs:decimal":
        q = type_el.find(V8 + "NumberQualifiers")
        digits, frac = 10, 0
        if q is not None:
            digits = int(_txt(q.find(V8 + "Digits")) or "10")
            frac = int(_txt(q.find(V8 + "FractionDigits")) or "0")
        report["Number"] += 1
        return {"type": "Number", "precision": digits, "scale": frac}
    if prim == "xs:dateTime":
        report["Date"] += 1
        return {"type": "Date"}
    if prim == "xs:boolean":
        report["Boolean"] += 1
        return {"type": "Boolean"}

    # Anything else (ValueStorage, UUID, CharacteristicRef, DefinedType.*,
    # ChartOf*Ref, ...) -> String fallback.
    report["fallback:" + (prim.split(":", 1)[-1].split(".", 1)[0])] += 1
    return {"type": "String", "length": 0}


def parse_props_name(obj_el):
    props = obj_el.find(MD + "Properties")
    if props is None:
        return None
    return _txt(props.find(MD + "Name")).strip() or None


def parse_attribute(attr_el):
    props = attr_el.find(MD + "Properties")
    if props is None:
        return None
    name = _txt(props.find(MD + "Name")).strip()
    if not name:
        return None
    d = map_type(props.find(MD + "Type"))
    d["name"] = name
    return d


def child_objects(obj_el):
    return obj_el.find(MD + "ChildObjects")


def _form_type_from_name(name):
    low = name.lower()
    if "списк" in low or "list" in low:
        return "list"
    if "выбор" in low or "choice" in low or "select" in low:
        return "select"
    if "групп" in low or "folder" in low:
        return "folder"
    return "object"


# 1C managed-form control tree (Ext/Form.xml, "logform" namespace) -> OES controls.
LF = "{http://v8.1c.ru/8.3/xcf/logform}"

# 1C control tag -> OES control kind understood by metadataConfigSpec BuildControlNode.
_CTRL_KIND = {
    "InputField":      "field",
    "LabelField":      "field",
    "CheckBoxField":   "checkbox",
    "LabelDecoration": "label",
    "UsualGroup":      "group",
    "ColumnGroup":     "group",
    "ButtonGroup":     "group",
    "Pages":           "pages",
    "Page":            "page",
    "Table":           "table",
}


def _last_seg(path):
    return path.rsplit(".", 1)[-1] if path else ""


def _data_path(el):
    dp = el.find(LF + "DataPath")
    return _txt(dp).strip() if dp is not None else ""


# 1C localized strings live under the "data/core" namespace as <item><lang>/<content>.
CORE = "{http://v8.1c.ru/8.1/data/core}"


def _title_loc(el):
    """Return a 1C LabelDecoration/group Title as an OES raw-loc-text string.

    OES stores a translatable caption (ibPropertyTString) as `code = 'text';`
    segments — one per language — which GetValueAsTranslateString parses back.
    A plain string would fail that parse and resolve to an EMPTY caption, so the
    per-language form is required. 1C lang codes (ru/ro/en) map straight across.
    Single quotes in the text are doubled, matching the loc-text quoting."""
    title = el.find(LF + "Title")
    if title is None:
        return ""
    parts = []
    for it in title:
        if _local(it.tag) != "item":
            continue
        lang = it.find(CORE + "lang")
        cont = it.find(CORE + "content")
        code = (_txt(lang).strip() if lang is not None else "")
        text = (_txt(cont) if cont is not None else "")
        if not code or not text:
            continue
        parts.append("%s = '%s';" % (code, text.replace("'", "''")))
    return "".join(parts)


def _map_form_children(child_items, in_table):
    """Map a <ChildItems> element to a list of OES control dicts.

    DataPath binds by its LAST segment (the attribute / column name), which is the
    raw 1C name — metadata names are imported verbatim (not translated), so the
    names match. Inside a Table only columns are meaningful: groups are flattened
    so the tablebox holds columns directly (its only legal child kind)."""
    out = []
    for el in child_items:
        kind = _CTRL_KIND.get(_local(el.tag))
        if kind is None:
            continue
        name = el.get("name") or ""
        if in_table:
            if kind in ("field", "checkbox"):
                out.append({"kind": "column", "name": name, "field": _last_seg(_data_path(el))})
            elif kind in ("group", "pages", "page"):
                sub = el.find(LF + "ChildItems")
                if sub is not None:
                    out.extend(_map_form_children(sub, True))   # flatten into columns
            continue
        node = {"kind": kind, "name": name}
        if kind in ("field", "checkbox"):
            node["attr"] = _last_seg(_data_path(el))
        elif kind == "table":
            node["attr"] = _last_seg(_data_path(el))
        elif kind == "label":
            # A LabelDecoration carries its own caption (bold section headers, the
            # "%" markers between fields). Carry it as raw-loc-text so the emitted
            # Statictext shows the real text instead of the default placeholder.
            title = _title_loc(el)
            if title:
                node["title"] = title
        sub = el.find(LF + "ChildItems")
        if sub is not None:
            children = _map_form_children(sub, kind == "table")
            if children:
                node["children"] = children
        out.append(node)
    return out


def parse_form_controls(form_xml_path):
    """Return the OES control tree for a 1C managed form's Ext/Form.xml, or []."""
    if not os.path.isfile(form_xml_path):
        return []
    try:
        root = ET.parse(form_xml_path).getroot()
    except ET.ParseError:
        return []
    child_items = root.find(LF + "ChildItems")   # the Form's own root items (not the command bar's)
    if child_items is None:
        return []
    return _map_form_children(child_items, False)


def parse_forms(dump_dir, kind_dir, base_name, limit=0):
    """Return [{name, type, module}] for an object's Forms/ (managed forms)."""
    forms_dir = os.path.join(dump_dir, kind_dir, base_name, "Forms")
    if not os.path.isdir(forms_dir):
        return []
    out = []
    for fn in sorted(os.listdir(forms_dir)):
        if not fn.endswith(".xml"):
            continue
        name = fn[:-4]
        module = read_file(os.path.join(forms_dir, name, "Ext", "Form", "Module.bsl"))
        form_type = _form_type_from_name(name)
        entry = {
            "name": name,
            "type": form_type,
            "module": maybe_translate(module),
        }
        # MVP-B: replicate the 1C control tree into FormData for object/item forms.
        # List/select/folder forms rely on OES auto-layout (their main source is the
        # list, which the control-binding model does not target yet).
        if form_type == "object":
            controls = parse_form_controls(os.path.join(forms_dir, name, "Ext", "Form.xml"))
            if controls:
                entry["controls"] = controls
                report["FormControls"] += 1
        out.append(entry)
        report["Forms"] += 1
        if limit and len(out) >= limit:
            break
    return out


def parse_record_object(root_el, dump_dir, kind_dir, base_name):
    """Catalog / Document -> friendly dict (attributes, tabularSections, modules)."""
    obj_el = None
    for child in root_el:
        obj_el = child
        break
    if obj_el is None:
        return None
    name = parse_props_name(obj_el)
    if not name:
        return None
    out = {"name": name, "attributes": [], "tabularSections": []}
    co = child_objects(obj_el)
    if co is not None:
        for el in co:
            tag = _local(el.tag)
            if tag == "Attribute":
                a = parse_attribute(el)
                if a:
                    out["attributes"].append(a)
            elif tag == "TabularSection":
                ts_name = parse_props_name(el)
                if not ts_name:
                    continue
                ts = {"name": ts_name, "attributes": []}
                tco = child_objects(el)
                if tco is not None:
                    for tel in tco:
                        if _local(tel.tag) == "Attribute":
                            a = parse_attribute(tel)
                            if a:
                                ts["attributes"].append(a)
                out["tabularSections"].append(ts)
    # modules
    ext = os.path.join(dump_dir, kind_dir, base_name, "Ext")
    obj_mod = read_file(os.path.join(ext, "ObjectModule.bsl"))
    mgr_mod = read_file(os.path.join(ext, "ManagerModule.bsl"))
    if obj_mod:
        out["objectModule"] = maybe_translate(obj_mod)
    if mgr_mod:
        out["managerModule"] = maybe_translate(mgr_mod)
    forms = parse_forms(dump_dir, kind_dir, base_name)
    if forms:
        out["forms"] = forms
    return out


def parse_enum(root_el):
    obj_el = next(iter(root_el), None)
    if obj_el is None:
        return None
    name = parse_props_name(obj_el)
    if not name:
        return None
    values = []
    co = child_objects(obj_el)
    if co is not None:
        for el in co:
            if _local(el.tag) == "EnumValue":
                vn = parse_props_name(el)
                if vn:
                    values.append(vn)
    return {"name": name, "values": values}


def parse_constant(root_el):
    obj_el = next(iter(root_el), None)
    if obj_el is None:
        return None
    props = obj_el.find(MD + "Properties")
    if props is None:
        return None
    name = _txt(props.find(MD + "Name")).strip()
    if not name:
        return None
    d = map_type(props.find(MD + "Type"))
    d["name"] = name
    return d


def parse_register(root_el, dump_dir, kind_dir, base_name):
    obj_el = next(iter(root_el), None)
    if obj_el is None:
        return None
    name = parse_props_name(obj_el)
    if not name:
        return None
    out = {"name": name, "dimensions": [], "resources": [], "attributes": []}
    co = child_objects(obj_el)
    if co is not None:
        for el in co:
            tag = _local(el.tag)
            if tag == "Dimension":
                a = parse_attribute(el)
                if a:
                    out["dimensions"].append(a)
            elif tag == "Resource":
                a = parse_attribute(el)
                if a:
                    out["resources"].append(a)
            elif tag == "Attribute":
                a = parse_attribute(el)
                if a:
                    out["attributes"].append(a)
    ext = os.path.join(dump_dir, kind_dir, base_name, "Ext")
    rs = read_file(os.path.join(ext, "RecordSetModule.bsl")) or read_file(os.path.join(ext, "ObjectModule.bsl"))
    mgr = read_file(os.path.join(ext, "ManagerModule.bsl"))
    if rs:
        out["objectModule"] = maybe_translate(rs)
    if mgr:
        out["managerModule"] = maybe_translate(mgr)
    forms = parse_forms(dump_dir, kind_dir, base_name)
    if forms:
        out["forms"] = forms
    return out


def parse_common_module(root_el, dump_dir, base_name):
    obj_el = next(iter(root_el), None)
    if obj_el is None:
        return None
    name = parse_props_name(obj_el)
    if not name:
        return None
    code = read_file(os.path.join(dump_dir, "CommonModules", base_name, "Ext", "Module.bsl"))
    return {"name": name, "code": maybe_translate(code)}


def iter_object_xml(dump_dir, kind_dir, limit):
    d = os.path.join(dump_dir, kind_dir)
    if not os.path.isdir(d):
        return
    n = 0
    for fn in sorted(os.listdir(d)):
        if not fn.endswith(".xml"):
            continue
        base = fn[:-4]
        yield base, os.path.join(d, fn)
        n += 1
        if limit and n >= limit:
            return


def load_root(path):
    try:
        return ET.parse(path).getroot()
    except ET.ParseError as ex:
        sys.stderr.write("WARN: parse error %s: %s\n" % (path, ex))
        return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("dump_dir")
    ap.add_argument("out_json")
    ap.add_argument("--only", default="", help="comma list of kinds to include")
    ap.add_argument("--limit", type=int, default=0, help="max objects per kind (smoke)")
    args = ap.parse_args()

    only = set(x.strip() for x in args.only.split(",") if x.strip())

    def want(kind):
        return not only or kind in only

    spec = {"name": "ImportedConfiguration"}

    # Configuration name
    cfg_xml = os.path.join(args.dump_dir, "Configuration.xml")
    root = load_root(cfg_xml) if os.path.exists(cfg_xml) else None
    if root is not None:
        cfg_el = next(iter(root), None)
        if cfg_el is not None:
            nm = parse_props_name(cfg_el)
            if nm:
                spec["name"] = nm

    if want("Catalogs"):
        spec["catalogs"] = []
        for base, path in iter_object_xml(args.dump_dir, "Catalogs", args.limit):
            r = load_root(path)
            if r is None:
                continue
            o = parse_record_object(r, args.dump_dir, "Catalogs", base)
            if o:
                spec["catalogs"].append(o)
                report["Catalogs"] += 1

    if want("Documents"):
        spec["documents"] = []
        for base, path in iter_object_xml(args.dump_dir, "Documents", args.limit):
            r = load_root(path)
            if r is None:
                continue
            o = parse_record_object(r, args.dump_dir, "Documents", base)
            if o:
                spec["documents"].append(o)
                report["Documents"] += 1

    if want("Enums"):
        spec["enums"] = []
        for base, path in iter_object_xml(args.dump_dir, "Enums", args.limit):
            r = load_root(path)
            if r is None:
                continue
            o = parse_enum(r)
            if o:
                spec["enums"].append(o)
                report["Enums"] += 1

    if want("Constants"):
        spec["constants"] = []
        for base, path in iter_object_xml(args.dump_dir, "Constants", args.limit):
            r = load_root(path)
            if r is None:
                continue
            o = parse_constant(r)
            if o:
                spec["constants"].append(o)
                report["Constants"] += 1

    if want("InformationRegisters"):
        spec["informationRegisters"] = []
        for base, path in iter_object_xml(args.dump_dir, "InformationRegisters", args.limit):
            r = load_root(path)
            if r is None:
                continue
            o = parse_register(r, args.dump_dir, "InformationRegisters", base)
            if o:
                spec["informationRegisters"].append(o)
                report["InformationRegisters"] += 1

    if want("AccumulationRegisters"):
        spec["accumulationRegisters"] = []
        for base, path in iter_object_xml(args.dump_dir, "AccumulationRegisters", args.limit):
            r = load_root(path)
            if r is None:
                continue
            o = parse_register(r, args.dump_dir, "AccumulationRegisters", base)
            if o:
                spec["accumulationRegisters"].append(o)
                report["AccumulationRegisters"] += 1

    if want("CommonModules"):
        spec["commonModules"] = []
        for base, path in iter_object_xml(args.dump_dir, "CommonModules", args.limit):
            r = load_root(path)
            if r is None:
                continue
            o = parse_common_module(r, args.dump_dir, base)
            if o:
                spec["commonModules"].append(o)
                report["CommonModules"] += 1

    with open(args.out_json, "w", encoding="utf-8") as f:
        json.dump(spec, f, ensure_ascii=False)

    sys.stderr.write("=== import report ===\n")
    for k in sorted(report):
        sys.stderr.write("  %-24s %d\n" % (k, report[k]))

    if TRANSLATE_BSL:
        miss = bsl_to_ves.missing_report()
        if miss:
            total = sum(c for _, c in miss)
            sys.stderr.write("=== untranslated word-parts: %d distinct, %d total "
                             "(add to tools/ru_en_dict.json) — top 40 ===\n"
                             % (len(miss), total))
            for word, cnt in miss[:40]:
                sys.stderr.write("  %-24s %d\n" % (word, cnt))

    sys.stderr.write("wrote %s\n" % args.out_json)


if __name__ == "__main__":
    main()
