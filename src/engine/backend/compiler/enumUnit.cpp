////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : enum unit
////////////////////////////////////////////////////////////////////////////

#include "enumUnit.h"

ibValueEnumerationWrapper::ibValueEnumerationWrapper(bool /*createInstance*/) :
	ibValueDynamicMembers(ibValueTypes::TYPE_VALUE, true)
{
	m_members.Bind(this, &ibValueEnumerationWrapper::FillMembers);
}

ibValueEnumerationWrapper::~ibValueEnumerationWrapper()
{
}

void ibValueEnumerationWrapper::FillMembers(ibMemberTable& helper) const
{
	// Primary value names, in declaration order (== sorted m_listEnumData order that GetPropVal
	// advances by), each pinned to its explicit index.
	long i = 0;
	for (auto& obj : m_listEnumStr)
		helper.AppendProp(obj, i++);
	// Extra names (e.g. Russian aliases) point at the same value slot as their primary.
	for (auto& a : m_listEnumAlias)
		helper.AppendProp(a.first, a.second);
}