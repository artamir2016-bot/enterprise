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

# Translation is OFF by default: OES natively runs Russian BSL (Cyrillic identifiers, ~35 Russian
# keyword aliases, 64 Russian system-function aliases, event names ПередЗаписью… via ibRuEventAlias).
# Translating identifiers to English BREAKS binding — an event handler ПередЗаписью becomes an
# unrecognised "BeforeRecord", and an attribute reference НомерРейса becomes "NumberReysa" that no
# longer matches the verbatim-Cyrillic metadata ("Переменная не определена"). Raw Russian modules
# compile and bind natively (verified). Opt back into translation with --translate-bsl.
TRANSLATE_BSL = False


def maybe_translate(code):
    if not code:
        return code
    # Always resolve 1C conditional compilation / region markers (OES has no client/server split);
    # this touches directives only, never identifiers, so verbatim binding is preserved.
    code = bsl_to_ves.preprocess_onec_module(code)
    if TRANSLATE_BSL:
        return bsl_to_ves.translate(code)
    return code

MD = "{http://v8.1c.ru/8.3/MDClasses}"
V8 = "{http://v8.1c.ru/8.1/data/core}"

# 1C reference type prefix -> OES friendly ref namespace (MVP targets only).
REF_PREFIX = {
    "CatalogRef": "Catalog",
    "DocumentRef": "Document",
    "EnumRef": "Enum",
    "ChartOfCharacteristicTypesRef": "ChartOfCharacteristicTypes",
    "ChartOfAccountsRef": "ChartOfAccounts",
    "ChartOfCalculationTypesRef": "ChartOfCalculationTypes",
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
    "Button":          "button",
}


# --- form-element event handlers (1C <Events>/<Event name=..> -> OES control events) ----------
# OES borrowed 1C's English event ids, so most map by identity; only a few differ per control kind.
# We only emit events the OES control actually declares (frontend widgets.h / tableBox.h / notebook.h),
# so an unknown 1C event is dropped rather than producing a dead property. The HANDLER (the text of
# the <Event>) is a form-module procedure name; module code is imported verbatim by default (OES runs
# Russian natively), so the handler stays verbatim too — it must match the procedure name in the module.
_OES_EVENTS = {
    "field":    {"OnChange", "StartChoice", "StartListChoice", "Clearing", "Opening", "ChoiceProcessing"},
    "checkbox": {"OnCheckboxClicked"},
    "table":    {"Selection", "OnActivateRow", "BeforeAddRow", "BeforeDeleteRow", "OnAddRow",
                 "OnDeleteRow", "OnChange", "StartChoice", "StartListChoice", "Clearing",
                 "Opening", "ChoiceProcessing"},
    "pages":    {"OnPageChanged"},
}
# 1C event id -> OES event id, per kind, only where they differ from identity.
_EVENT_RENAME = {
    "checkbox": {"OnChange": "OnCheckboxClicked"},
    "pages":    {"OnCurrentPageChange": "OnPageChanged"},
}

# Report counters for imported / dropped form events.
_EVENT_STATS = {"imported": 0, "dropped": {}}


def _ev_handler(text):
    """The handler procedure name, translated the same way module code is (verbatim by default)."""
    name = (text or "").strip()
    if not name:
        return ""
    return bsl_to_ves.translate_identifier(name) if TRANSLATE_BSL else name


def _control_events(el, kind):
    """Return {oes_event_name: handler_proc} for a control element's <Events>, or {}."""
    evs = el.find(LF + "Events")
    if evs is None:
        return {}
    allowed = _OES_EVENTS.get(kind)
    if not allowed:
        return {}
    rename = _EVENT_RENAME.get(kind, {})
    out = {}
    for ev in evs:
        if _local(ev.tag) != "Event":
            continue
        onec_name = ev.get("name") or ""
        handler = _ev_handler(_txt(ev))
        if not onec_name or not handler:
            continue
        oes_name = rename.get(onec_name, onec_name)
        if oes_name in allowed:
            out[oes_name] = handler
            _EVENT_STATS["imported"] += 1
        else:
            _EVENT_STATS["dropped"][onec_name] = _EVENT_STATS["dropped"].get(onec_name, 0) + 1
    return out


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
            # An explicit field Title overrides the bound attribute's synonym as the
            # caption ("Род:", "один:", "два:"). 1C sets it on form-attribute fields
            # whose attribute name isn't a good label; catalog-bound fields usually
            # have none and fall back to the synonym. Carry it as raw-loc-text.
            ftitle = _title_loc(el)
            if ftitle:
                node["title"] = ftitle
        elif kind == "table":
            node["attr"] = _last_seg(_data_path(el))
        elif kind == "label":
            # A LabelDecoration carries its own caption (bold section headers, the
            # "%" markers between fields). Carry it as raw-loc-text so the emitted
            # Statictext shows the real text instead of the default placeholder.
            title = _title_loc(el)
            if title:
                node["title"] = title
        elif kind == "page":
            # A Page's Title is its TAB caption; without it the tab shows OES's
            # default "New page". Carry it as raw-loc-text (same as decorations).
            title = _title_loc(el)
            if title:
                node["title"] = title
        elif kind == "button":
            # A button carries no data — it DELEGATES its click to a form command,
            # referenced by <CommandName>Form.Command.XXX</CommandName>. Carry the
            # command's own name (last segment) so the importer binds it to the
            # emitted form command; its click then runs that command's Action handler.
            cmd = el.find(LF + "CommandName")
            if cmd is not None:
                node["command"] = _last_seg(_txt(cmd).strip())
            btitle = _title_loc(el)
            if btitle:
                node["title"] = btitle
        elif kind == "group":
            # A UsualGroup is now emitted as a REAL nested box (not flattened), so its
            # child layout has to carry the group's own properties:
            #   * <Group> child orientation — Vertical (default) / Horizontal /
            #     AlwaysHorizontal. Drives the box sizer orientation.
            #   * <ShowTitle> — whether a caption/frame is drawn. 1C's default (element
            #     absent) is on. Layout-only groups set it false → a plain, untitled box.
            #   * ColumnGroup lays its children out side-by-side → horizontal by nature.
            grp = el.find(LF + "Group")
            gval = _txt(grp).strip().lower() if grp is not None else ""
            horizontal = ("horizontal" in gval) or (_local(el.tag) == "ColumnGroup")
            node["orient"] = "horizontal" if horizontal else "vertical"

            show = el.find(LF + "ShowTitle")
            shown = (show is None) or (_txt(show).strip().lower() == "true")
            if shown:
                title = _title_loc(el)
                if title:
                    node["title"] = title
        # Event handlers — bind the 1C element's <Events> to the OES control's events so the
        # imported form procedures actually fire (field OnChange, checkbox click, table selection…).
        events = _control_events(el, kind)
        if events:
            node["events"] = events

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


def parse_form_commands(form_xml_path):
    """Return the form's COMMANDS as [{name, caption, action}], or [].

    A 1C managed form declares its commands under a top-level <Commands> element;
    each <Command name="X"> carries a <Title> (caption) and an <Action> naming the
    form-module handler its buttons run. Buttons reference the command by name, so
    this list plus the button's <CommandName> reconnect the click to its handler.
    The handler is a form-module procedure name — imported verbatim (same as module
    code), so it matches the procedure in the module."""
    if not os.path.isfile(form_xml_path):
        return []
    try:
        root = ET.parse(form_xml_path).getroot()
    except ET.ParseError:
        return []
    cont = root.find(LF + "Commands")
    if cont is None:
        return []
    out = []
    for c in cont:
        if _local(c.tag) != "Command":
            continue
        name = c.get("name") or ""
        if not name:
            continue
        act = c.find(LF + "Action")
        handler = _ev_handler(_txt(act)) if act is not None else ""
        node = {"name": name}
        if handler:
            node["action"] = handler
        title = _title_loc(c)
        if title:
            node["caption"] = title
        out.append(node)
    return out


def parse_form_attributes(form_xml_path):
    """Return the form's OWN attributes (the non-main <Attributes>) as OES type dicts.

    These are 1C form attributes (ПолеПрописи…, СуммаЧисло, …) that don't exist on
    the catalog: a field bound to one must resolve its type from the form attribute,
    or it renders as an empty (typeless) control. Primitive xs: types map straight to
    OES primitives; the main object attribute and composite/ref types are skipped
    (the main one is emitted separately; a ref form attribute isn't modelled here)."""
    if not os.path.isfile(form_xml_path):
        return []
    try:
        root = ET.parse(form_xml_path).getroot()
    except ET.ParseError:
        return []
    cont = root.find(LF + "Attributes")
    if cont is None:
        return []
    out = []
    for a in cont:
        if _local(a.tag) != "Attribute":
            continue
        main = a.find(LF + "MainAttribute")
        if main is not None and _txt(main).strip().lower() == "true":
            continue
        name = a.get("name") or ""
        if not name:
            continue
        t = a.find(LF + "Type")
        tt = t.find(CORE + "Type") if t is not None else None
        xstype = _txt(tt).strip() if tt is not None else ""
        node = {"name": name}
        if xstype == "xs:decimal":
            node["type"] = "Number"
            nq = t.find(CORE + "NumberQualifiers")
            if nq is not None:
                d = nq.find(CORE + "Digits")
                fd = nq.find(CORE + "FractionDigits")
                if d is not None and _txt(d).strip().isdigit():
                    node["precision"] = int(_txt(d).strip())
                if fd is not None and _txt(fd).strip().isdigit():
                    node["scale"] = int(_txt(fd).strip())
        elif xstype == "xs:boolean":
            node["type"] = "Boolean"
        elif xstype in ("xs:dateTime", "xs:date"):
            node["type"] = "Date"
        elif xstype == "xs:string":
            node["type"] = "String"
            sq = t.find(CORE + "StringQualifiers")
            if sq is not None:
                ln = sq.find(CORE + "Length")
                if ln is not None and _txt(ln).strip().isdigit():
                    node["length"] = int(_txt(ln).strip())
        else:
            # Main object, composite, or ref-typed form attribute: not modelled here.
            continue
        out.append(node)
    return out


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
            form_xml = os.path.join(forms_dir, name, "Ext", "Form.xml")
            controls = parse_form_controls(form_xml)
            if controls:
                entry["controls"] = controls
                report["FormControls"] += 1
            # The form's own attributes — so fields bound to them (not to a catalog
            # attribute) resolve a type and render instead of staying blank.
            fattrs = parse_form_attributes(form_xml)
            if fattrs:
                entry["formAttributes"] = fattrs
            # Form commands — buttons in the control tree delegate their click to these.
            commands = parse_form_commands(form_xml)
            if commands:
                entry["commands"] = commands
                report["FormCommands"] += 1
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


def parse_chart_cct(root_el, dump_dir, kind_dir, base_name):
    """ChartOfCharacteristicTypes -> record object (attributes/tabs/modules/forms)
    PLUS the value type its characteristics may hold (<Properties>/<Type>)."""
    o = parse_record_object(root_el, dump_dir, kind_dir, base_name)
    if o is None:
        return None
    obj_el = next(iter(root_el), None)
    if obj_el is not None:
        props = obj_el.find(MD + "Properties")
        if props is not None:
            t = props.find(MD + "Type")
            if t is not None:
                o["valueType"] = map_type(t)
    return o


def parse_chart_coa(root_el, dump_dir, kind_dir, base_name):
    """ChartOfAccounts -> record object + the mandatory chart-of-characteristic-types binding
    (<Properties>/<ExtDimensionTypes> already carries a 'ChartOfCharacteristicTypes.<Name>' ref key)."""
    o = parse_record_object(root_el, dump_dir, kind_dir, base_name)
    if o is None:
        return None
    obj_el = next(iter(root_el), None)
    if obj_el is not None:
        props = obj_el.find(MD + "Properties")
        if props is not None:
            ext = props.find(MD + "ExtDimensionTypes")
            key = _txt(ext).strip() if ext is not None else ""
            # 1C emits e.g. "ChartOfCharacteristicTypes.ВидыСубконто" — the exact refMap key.
            if key.startswith("ChartOfCharacteristicTypes."):
                o["chartOfCharacteristicTypes"] = key
    return o


def parse_subsystem(basedir, name):
    """Subsystem (Section) -> {name, subsystems:[...]}. Children are named in <ChildObjects>/<Subsystem>
    and their XML lives at <basedir>/<name>/Subsystems/<child>.xml (recursion mirrors the 1C layout).
    Content/rights are out of MVP — the tree shape is what carries across."""
    xml_path = os.path.join(basedir, name + ".xml")
    r = load_root(xml_path)
    if r is None:
        return None
    obj_el = next(iter(r), None)
    nm = parse_props_name(obj_el) if obj_el is not None else None
    if not nm:
        return None
    out = {"name": nm}
    child_dir = os.path.join(basedir, name, "Subsystems")
    kids = []
    co = obj_el.find(MD + "ChildObjects")
    if co is not None:
        for el in co:
            if _local(el.tag) == "Subsystem":
                child_name = (el.text or "").strip()
                if child_name:
                    kid = parse_subsystem(child_dir, child_name)
                    if kid:
                        kids.append(kid)
    if kids:
        out["subsystems"] = kids
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


# Import pure managed-client common modules too (OFF by default). A module that is
# visible ONLY in the managed-client context (Server/ServerCall/ExternalConnection/
# ClientOrdinaryApplication all false) holds thin-client UI code that references APIs
# OES's single-process runtime does not have; importing it — and worse, letting it go
# GLOBAL and compile eagerly at base open — risks crashing the whole session. So skip
# such modules unless the caller opts in.
INCLUDE_CLIENT_MODULES = False


def _bool_prop(props, tag):
    el = props.find(MD + tag) if props is not None else None
    return _txt(el).strip().lower() == "true"


def parse_common_module(root_el, dump_dir, base_name):
    obj_el = next(iter(root_el), None)
    if obj_el is None:
        return None
    name = parse_props_name(obj_el)
    if not name:
        return None

    # Visibility context — 1C CommonModule compilation-context matrix. OES has no
    # client/server split at runtime, so we do not carry the matrix verbatim; instead we
    # (a) DECIDE whether the module belongs in a single-process runtime at all, and
    # (b) map 1C Global -> OES GlobalModule, but only when the module is server-visible
    #     (a global module compiles EAGERLY at base open — a client-only global would
    #      fault the session), otherwise demote it to a plain, name-qualified module.
    props = obj_el.find(MD + "Properties")
    ctx = {
        "global": _bool_prop(props, "Global"),
        "server": _bool_prop(props, "Server"),
        "serverCall": _bool_prop(props, "ServerCall"),
        "externalConnection": _bool_prop(props, "ExternalConnection"),
        "clientOrdinary": _bool_prop(props, "ClientOrdinaryApplication"),
        "clientManaged": _bool_prop(props, "ClientManagedApplication"),
        "privileged": _bool_prop(props, "Privileged"),
    }
    server_visible = (ctx["server"] or ctx["serverCall"]
                      or ctx["externalConnection"] or ctx["clientOrdinary"])

    if not server_visible and not INCLUDE_CLIENT_MODULES:
        # Pure managed-client module — not runnable in OES's server-like runtime.
        report["CommonModulesSkippedClient"] += 1
        sys.stderr.write("SKIP client-only common module: %s\n" % name)
        return None

    is_global = ctx["global"] and server_visible
    if ctx["global"] and not server_visible:
        sys.stderr.write(
            "NOTE %s: 1C Global but client-only -> imported as non-global\n" % name)

    code = read_file(os.path.join(dump_dir, "CommonModules", base_name, "Ext", "Module.bsl"))
    return {
        "name": name,
        "code": maybe_translate(code),
        "global": is_global,
        "context": ctx,
    }


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
    ap.add_argument("--translate-bsl", action="store_true",
                    help="translate module code to English VES (OFF by default — OES runs Russian "
                         "natively; translation breaks handler/attribute binding)")
    ap.add_argument("--syntax", default="ves", choices=["ves", "ces"],
                    help="configuration script syntax (ves = Russian If/Then style; default)")
    ap.add_argument("--include-client-modules", action="store_true",
                    help="also import common modules visible ONLY in the managed-client "
                         "context (skipped by default — thin-client code that OES's "
                         "single-process runtime cannot resolve)")
    args = ap.parse_args()

    global TRANSLATE_BSL, INCLUDE_CLIENT_MODULES
    TRANSLATE_BSL = args.translate_bsl
    INCLUDE_CLIENT_MODULES = args.include_client_modules

    only = set(x.strip() for x in args.only.split(",") if x.strip())

    def want(kind):
        return not only or kind in only

    spec = {"name": "ImportedConfiguration", "syntax": args.syntax}

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

    if want("ChartsOfCharacteristicTypes"):
        spec["chartsOfCharacteristicTypes"] = []
        for base, path in iter_object_xml(args.dump_dir, "ChartsOfCharacteristicTypes", args.limit):
            r = load_root(path)
            if r is None:
                continue
            o = parse_chart_cct(r, args.dump_dir, "ChartsOfCharacteristicTypes", base)
            if o:
                spec["chartsOfCharacteristicTypes"].append(o)
                report["ChartsOfCharacteristicTypes"] += 1

    if want("ChartsOfAccounts"):
        spec["chartsOfAccounts"] = []
        for base, path in iter_object_xml(args.dump_dir, "ChartsOfAccounts", args.limit):
            r = load_root(path)
            if r is None:
                continue
            o = parse_chart_coa(r, args.dump_dir, "ChartsOfAccounts", base)
            if o:
                spec["chartsOfAccounts"].append(o)
                report["ChartsOfAccounts"] += 1

    if want("ChartsOfCalculationTypes"):
        spec["chartsOfCalculationTypes"] = []
        for base, path in iter_object_xml(args.dump_dir, "ChartsOfCalculationTypes", args.limit):
            r = load_root(path)
            if r is None:
                continue
            o = parse_record_object(r, args.dump_dir, "ChartsOfCalculationTypes", base)
            if o:
                spec["chartsOfCalculationTypes"].append(o)
                report["ChartsOfCalculationTypes"] += 1

    if want("CalculationRegisters"):
        spec["calculationRegisters"] = []
        for base, path in iter_object_xml(args.dump_dir, "CalculationRegisters", args.limit):
            r = load_root(path)
            if r is None:
                continue
            o = parse_register(r, args.dump_dir, "CalculationRegisters", base)
            if o:
                # Action-period flag (<Properties>/<ActionPeriod>): turns the record into an interval
                # and adds the action/registration-period standard columns.
                obj_el = next(iter(r), None)
                if obj_el is not None:
                    props = obj_el.find(MD + "Properties")
                    if props is not None:
                        ap = props.find(MD + "ActionPeriod")
                        if ap is not None and _txt(ap).strip().lower() == "true":
                            o["useActionPeriod"] = True
                spec["calculationRegisters"].append(o)
                report["CalculationRegisters"] += 1

    if want("Roles"):
        spec["roles"] = []
        for base, path in iter_object_xml(args.dump_dir, "Roles", args.limit):
            r = load_root(path)
            if r is None:
                continue
            nm = parse_props_name(next(iter(r), None))
            if nm:
                spec["roles"].append({"name": nm})
                report["Roles"] += 1

    if want("Subsystems"):
        spec["subsystems"] = []
        ss_dir = os.path.join(args.dump_dir, "Subsystems")
        for base, path in iter_object_xml(args.dump_dir, "Subsystems", args.limit):
            o = parse_subsystem(ss_dir, base)
            if o:
                spec["subsystems"].append(o)
                report["Subsystems"] += 1

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

    # Form event handlers: how many element events were bound, and which 1C events were dropped
    # (no OES equivalent on that control — candidates for a future mapping).
    sys.stderr.write("  %-24s %d\n" % ("FormEvents", _EVENT_STATS["imported"]))
    if _EVENT_STATS["dropped"]:
        drop = sorted(_EVENT_STATS["dropped"].items(), key=lambda kv: -kv[1])
        sys.stderr.write("=== form events with no OES mapping (dropped) — top 20 ===\n")
        for name, cnt in drop[:20]:
            sys.stderr.write("  %-24s %d\n" % (name, cnt))

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
