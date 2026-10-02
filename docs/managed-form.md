# Managed form — a declarative form metatype (1C «управляемая форма»)

> **Status: DESIGN + Increment 1 in progress (2026-09-23).** This doc is the arc. Sections
> marked BUILT are in the tree; the rest is the plan. Change a status here first, then the code.
>
> **Companions:** [form-engine.md](form-engine.md) (the runtime form this compiles INTO),
> [metadataConfigSpec.cpp](../src/engine/backend/metadataConfigSpec.cpp) (the control-tree blob
> shape — the TARGET), [form-attribute-binding.md](form-attribute-binding.md) (how a control
> reaches a value), [metadata-lifecycle.md](metadata-lifecycle.md) (metaobject events).

---

## 1. Why a second form metatype — and why it costs little

OES already has ONE form model: a control tree (`ibValueFrame`) laid out by **sizers and
proportions** (not pixel coordinates), rendered on desktop (wx) AND web (the `visualHost` walker
→ JSON → flex-HTML) from one description. That is already "managed-like".

So why add a metatype at all? Because 1C's **managed form** is a distinct *authoring* model that
the platform we import from uses natively: a form is a **tree of elements** — `Группа` / `Поле` /
`Таблица` / `Кнопка` / `Декорация` — where a **field's rendering is chosen by its ВидПоля** (view
kind: input / label / checkbox / picture / radio), NOT by a concrete control class, and there is
**no geometry at all**. Importing that faithfully, and authoring in the same terms, wants a model
shaped like it.

**The cheap part:** a managed form does not need a second rendering stack. Its element tree
**compiles into the exact `ibDataNode` control-tree blob** the existing form already loads
(`ibValueMetaObjectFormBase::FormBlobToNode` / `LoadControl`, formMem.cpp). `metadataConfigSpec.cpp`
already builds such a blob from imported 1C forms with correct group nesting. So:

```
ManagedForm element tree  ──compile──▶  control-tree ibDataNode blob  ──LoadForm──▶  ibValueFrame tree
   (declarative, no geom)                 (existing FormData shape)        (desktop wx + web JSON, free)
```

The managed metatype is therefore **a declarative SOURCE model + a compiler into the existing
TARGET** — not a parallel runtime. Everything downstream (the control port work, the web
renderers, the command door, access policy) is reused unchanged.

---

## 2. The element model (the managed AST)

A managed form owns a tree of `ibManagedElement` nodes plus a flat list of **form attributes**
(the data the form binds to). No node carries position or size.

| Node | 1C | Compiles to | Carries |
|---|---|---|---|
| **FormAttribute** | Реквизит формы | a form attribute (`ibFormAttributeValue`) | name, type, isMain |
| **Group** | Группа формы | `CT_BSZR` (box sizer) or `CT_SSZER` (titled) or `CT_NTBK` (pages) | direction (V/H), grouping (none/box/pages), title |
| **Page** | Страница | `CT_NTPG` (notebook page) | title |
| **Field** | Поле формы | control chosen by **ViewKind** (see §3) | dataPath, viewKind, title |
| **Table** | Таблица формы | `CT_TABL` (tablebox) | dataPath (tabular attr), columns |
| **Column** | Колонка | `CT_TBLC` | dataPath (column), viewKind |
| **Button** | Кнопка | `CT_BUTN` | command binding, title |
| **Decoration** | Декорация | `CT_STTX` (label) / picture | title / picture, no binding |

Serialised through `ibDataNode` like everything else (§6). Stored on the metaobject as its own
property blob, SEPARATE from the compiled FormData (which is a cache, rebuilt on save).

---

## 3. ViewKind — the one genuinely new concept

1C's ВидПоля decides how a bound field draws. OES has no equivalent today (auto-form picks a
control by the source TYPE, hardcoded in `formObject.cpp` — boolean→checkbox, reference→textctrl+
select, tabular→tablebox). ManagedForm makes that choice **explicit and declarative**:

```
enum ibFieldViewKind {
  Auto,           // pick by type, current behaviour (the default)
  InputField,     // CT_TXTC  (text box, + select button for references)
  LabelField,     // CT_STTX  (read-only presentation)
  CheckBoxField,  // CT_CHKB
  RadioField,     // CT_RDBT
  PictureField,   // picture control
  ChoiceField,    // CT_CHOI  (drop-down list — the control ported 2026-09-23)
  ComboBoxField,  // CT_CMBB
}
```

`Auto` reuses the existing type-driven selection so an unspecified field behaves exactly as an
auto-built form does. Import maps 1C ВидПоля onto the explicit kinds; anything unknown falls back
to `Auto`. This is the enum an imported managed form finally *honours* — today the importer
rebuilds fields from the source type and drops 1C's ВидПоля entirely.

**Auto is live (1c, landed 2026-09-24).** The compiler takes an `AutoKindResolver` (dataPath ->
concrete kind); the metatype builds it from the owner's attribute types, mirroring the auto-form
rule in `formObject.cpp` — a single **boolean** draws as a checkbox, a primitive (number/string/date)
as a plain input, a **reference** as an input WITH a select button (`ReferenceField`). An explicit
ViewKind is honoured as-is. **`PictureField` draws through a real Picture control** (`CT_PICT`,
desktop `wxStaticBitmap` / web `<img>`) — the tails landed 2026-09-24. Open only: the runtime fetch
of a *bound* picture value (a static Picture property renders today; the bound image is a later step).

---

## 4. The compiler (element tree → control-tree blob)

One function, `ibManagedFormCompiler::Compile(rootElement, attributes) -> ibDataNode`, mirroring
`metadataConfigSpec.cpp::BuildControlNode`:

- walk the element tree depth-first;
- for each node emit the target control node (`AddChild(clsid, id)`), its `Source` binding
  (`MakeSource(hops)` — the metaId path into a form attribute + field), and wrap sizerable
  children in a `CT_SIZR` item (`AddSizerItem`);
- a Group becomes a box sizer with the child orientation; a titled group a static-box sizer; a
  pages group a notebook with `CT_NTPG` children;
- a Field's clsid comes from its ViewKind (§3), `Auto` deferring to the type rule;
- the result is the same blob shape `SetFormData` stores and `LoadForm` reads.

Reusing the `metadataConfigSpec` helpers means the compiler and the importer emit **identical**
node shapes — one target, tested once.

---

## 5. Metatype wiring (the recipe)

Following `ibValueMetaObjectForm` and the simple-metatype recipe (constant.*):

- `partial/managedForm.{h,cpp}` — `ibValueMetaObjectManagedForm : ibValueMetaObjectFormBase`.
  Stores the **element tree** blob (`m_propertyElements`) + a module + a compiled FormData cache.
  `GetObjectForm` / `CreateAndBuildForm` compile on demand; `GetTypeForm` returns the managed type.
- `metaObject.h` — `constexpr ibClassID g_metaManagedFormCLSID = metadata_to_clsid("MD_MFRM");`
- `managedForm.cpp` — `METADATA_TYPE_REGISTER(ibValueMetaObjectManagedForm, "ManagedForm", g_metaManagedFormCLSID);`
- The element node classes live in `backend/managedForm/` (GUI-free — the compiler is backend, so
  the web server builds it too).

A managed form can belong to an object (like `ibValueMetaObjectForm`) or stand alone (a common
managed form) — start with the object-owned kind, mirror the common kind later.

---

## 6. Serialization

The element tree serialises through `ibDataNode` (the format-agnostic tree every metaobject uses).
`ReadData`/`WriteData` on the metaobject write the element blob and the module; the compiled
FormData is NOT serialised (it is a cache, rebuilt from the elements on save — the same way the
AOT bytecode cache relates to source).

---

## 7. Increment plan

1. **Foundation (IN PROGRESS).** The element node model (`ibManagedElement` + kinds + ViewKind
   enum), the metatype `ibValueMetaObjectManagedForm` (element blob + module + serialization +
   registration), and `ibManagedFormCompiler` for the core subset — Group (V/H) + Field(Auto/
   Input/Label/CheckBox) — verified by a gtest that compiles a hand-built tree into a blob and
   asserts the node shape (clsids, Source hops, nesting) matches the metadataConfigSpec target.
2. **Runtime open.** Wire `GetObjectForm` so a managed form actually opens as a live `ibValueForm`
   on desktop; verify a hand-authored managed catalog form renders.
3. **ViewKind Auto (live) — LANDED 1c (2026-09-24).** The compiler's `AutoKindResolver` +
   the metatype building it from owner attribute types (boolean -> checkbox, else input). Table /
   Button / Pages / Decoration already emit (§2). Still open: the reference select button and a
   real picture control.
4. **Import.** Map 1C managed-form XML (`onec_to_spec.py` already reads the ChildItems tree) onto
   the element model, honouring ВидПоля — a managed form imports to a ManagedForm metaobject
   instead of being flattened into a synthesised control tree.
5. **Designer editor — LANDED (2026-10-03).** `ibManagedFormEditor` registered for
   `g_metaManagedFormCLSID` (designer/win/editor/managedFormEditor/, docView in
   docManager/templates/docViewManagedFormEditor). An element-tree `wxTreeCtrl` over a working
   copy; toolbar + context menu add **all** node kinds (Group / Field / Table / Column / Button /
   Pages / Page / Decoration) with containment rules (Column only in a Table, Page only in a Pages
   group); the selected node edits in the shared objectInspector via `ibManagedElementProperty`,
   with **DataPath a dropdown** of the owner's attributes + Section.Column (not free text);
   **undo/redo** (snapshot stack, toolbar + Ctrl+Z/Y); **Test form** compiles + ShowForm()s a live
   preview. Every edit flushes to the metaobject (`MarkDirty` → `SetElementTree`) so a config
   save/Update persists it. Verified live (no computer-use) via `tools/oes_testrunner/gui_probe.py`.
6. **Web + thin parity check — LANDED (2026-10-03).** Round-trip verified: an edit in the designer
   persisted via Update database configuration survives a reopen; the managed ItemForm renders on
   `wenterprise-server` as `textctrl "Price"` / `textctrl "Note"` / `checkbox "Active"` inside the
   MainGroup box — compiled at runtime from the element tree (`GET /form/<id>`).

Increments 1–3 are backend-only and testable without a GUI, which is why they come first.
