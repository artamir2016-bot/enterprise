#include "managedFormSerializer.h"

#include "backend/clsid.h"                     // system_to_clsid
#include "backend/serialize/dataBuilder.h"     // ibDataNode

namespace {

// Node clsids used purely to tag the serialised sub-nodes — internal, never
// registered as a runtime type.
const ibClassID kElemClsid = system_to_clsid("MgdElem");
const ibClassID kAttrClsid = system_to_clsid("MgdAttr");

} // namespace

void ibManagedFormSerializer::WriteElement(ibDataNode& node, const ibManagedElement& el) {
	node.SetValue<s32>(wxT("Kind"),     static_cast<s32>(el.kind));
	node.SetValue(wxT("Name"),          el.name);
	node.SetValue(wxT("Title"),         el.title);
	node.SetValue(wxT("DataPath"),      el.dataPath);
	node.SetValue<s32>(wxT("ViewKind"), static_cast<s32>(el.viewKind));
	node.SetValue<s32>(wxT("Layout"),   static_cast<s32>(el.layout));
	node.SetValue<s32>(wxT("Repr"),     static_cast<s32>(el.representation));
	node.SetValue<s32>(wxT("CommandId"), static_cast<s32>(el.commandId));
	for (std::size_t i = 0; i < el.children.size(); ++i) {
		ibDataNode& cn = node.AddChild(kElemClsid, static_cast<ibMetaID>(i));
		WriteElement(cn, el.children[i]);
	}
}

void ibManagedFormSerializer::ReadElement(const ibDataNode& node, ibManagedElement& el) {
	el.kind           = static_cast<ibManagedNodeKind>(node.GetValue<s32>(wxT("Kind")));
	el.name           = node.GetValue<wxString>(wxT("Name"));
	el.title          = node.GetValue<wxString>(wxT("Title"));
	el.dataPath       = node.GetValue<wxString>(wxT("DataPath"));
	el.viewKind       = static_cast<ibFieldViewKind>(node.GetValue<s32>(wxT("ViewKind")));
	el.layout         = static_cast<ibGroupLayout>(node.GetValue<s32>(wxT("Layout")));
	el.representation = static_cast<ibGroupRepresentation>(node.GetValue<s32>(wxT("Repr")));
	el.commandId      = static_cast<ibMetaID>(node.GetValue<s32>(wxT("CommandId")));
	el.children.clear();
	for (const ibDataNode& cn : node.Children()) {
		if (cn.GetClsid() != kElemClsid)
			continue;
		el.children.emplace_back();
		ReadElement(cn, el.children.back());
	}
}

void ibManagedFormSerializer::Write(ibDataNode& node, const ibManagedElement& root,
	const std::vector<ibManagedAttribute>& attrs) {
	WriteElement(node.Child(wxT("Root")), root);
	ibDataNode& attrsNode = node.Child(wxT("Attrs"));
	for (std::size_t i = 0; i < attrs.size(); ++i) {
		ibDataNode& an = attrsNode.AddChild(kAttrClsid, static_cast<ibMetaID>(i));
		an.SetValue(wxT("Name"), attrs[i].name);
		an.SetValue<s32>(wxT("Id"), static_cast<s32>(attrs[i].id));
		an.SetValue(wxT("Main"), attrs[i].isMain);
		an.SetValue<s32>(wxT("Type"), static_cast<s32>(attrs[i].type));
	}
}

void ibManagedFormSerializer::Read(const ibDataNode& node, ibManagedElement& root,
	std::vector<ibManagedAttribute>& attrs) {
	root = ibManagedElement();
	attrs.clear();
	if (const ibDataNode* rootNode = node.FindChild(wxT("Root")))
		ReadElement(*rootNode, root);
	if (const ibDataNode* attrsNode = node.FindChild(wxT("Attrs"))) {
		for (const ibDataNode& an : attrsNode->Children()) {
			if (an.GetClsid() != kAttrClsid)
				continue;
			ibManagedAttribute attr;
			attr.name   = an.GetValue<wxString>(wxT("Name"));
			attr.id     = static_cast<ibMetaID>(an.GetValue<s32>(wxT("Id")));
			attr.isMain = an.GetValue<bool>(wxT("Main"));
			const s32 rawType = an.GetValue<s32>(wxT("Type"));   // 0 (absent / legacy) → String
			attr.type   = rawType != 0 ? static_cast<ibValueTypes>(rawType) : ibValueTypes::TYPE_STRING;
			attrs.push_back(attr);
		}
	}
}
