#include "compileModule.h"
#include "backend/metaCollection/metaModuleObject.h"
#include "appData.h"

#pragma warning(push)
#pragma warning(disable : 4018)

ibCompileModule::ibCompileModule(const ibValueMetaObjectModuleBase* moduleObject, bool onlyFunction) :
	ibCompileCode(moduleObject->GetFullName(), moduleObject->GetDocPath(), onlyFunction),
	m_moduleObject(moduleObject)
{
	m_cByteCode.m_strModuleName = m_moduleObject->GetFullName();

	m_strModuleName = m_moduleObject->GetFullName();
	m_strDocPath = m_moduleObject->GetDocPath();
	m_strFileName = m_moduleObject->GetFileName();

	Load(m_moduleObject->GetModuleText());

	//We don’t look for local variables in parent contexts!
	m_rootContext->m_numFindLocalInParent = 0;
}

/**
 * Compile
 * Purpose:
 * Translation and compilation of source code into bytecode (object code)
 * Return value:
 * true,false
 */

bool ibCompileModule::Compile()
{
	//clear functions & variables 
	Reset();

	if (m_moduleObject != nullptr &&
		m_moduleObject->IsGlobalModule()) {

		m_strModuleName = m_moduleObject->GetFullName();
		m_strDocPath = m_moduleObject->GetDocPath();
		m_strFileName = m_moduleObject->GetFileName();

		m_changedCode = false;

		Load(m_moduleObject->GetModuleText());

		if (m_parentModule == nullptr)
			return true;
		// A global module has no bytecode of its own — it is inlined into the parent (root) compile
		// unit, so "compiling" it means compiling the parent. But parent->Compile() Reset()s the parent's
		// bytecode FIRST; if that recompile then fails (another broken inlined global), the parent is left
		// EMPTY — wiping the whole system-function / alias table that every runtime-compiled module resolves
		// against, so imported catalog/document/form modules can no longer see СокрЛП / Сообщить / etc.
		// CreateMainModule already appends every global and compiles the root ONCE. So once the root is
		// compiled, re-driving it from each global is both redundant and destructive: skip it. Only cascade
		// while the root has not been built yet (the first global to compile triggers the initial build).
		if (m_parentModule->m_cByteCode.m_bCompile)
			return true;
		return m_parentModule->Compile();
	}

	//recursively compile modules in case of any changes
	if (m_parentModule != nullptr && appData->DesignerMode()) {

		std::stack<ibCompileModule*> compileModule; bool callRecompile = false;

		ibCompileModule* parentModule = GetParent();

		while (parentModule != nullptr) {
			if (parentModule->m_changedCode) callRecompile = true;
			if (callRecompile) compileModule.push(parentModule);
			parentModule = parentModule->GetParent();
		}

		while (!compileModule.empty()) {
			ibCompileModule* compileCode = compileModule.top();
			if (!compileCode->Recompile()) return false;
			compileModule.pop();
		}
	}

	if (m_moduleObject != nullptr) {

		m_cByteCode.m_strModuleName = m_moduleObject->GetFullName();

		if (m_parentModule != nullptr) {
			m_cByteCode.m_parent = &m_parentModule->m_cByteCode;
			m_rootContext->m_parentContext = m_parentModule->m_rootContext;
		}

		m_strModuleName = m_moduleObject->GetFullName();
		m_strDocPath = m_moduleObject->GetDocPath();
		m_strFileName = m_moduleObject->GetFileName();

		Load(m_moduleObject->GetModuleText());
	}

	//prepare lexem
	if (!PrepareLexem()) {
		return false;
	}

	//prepare context variables
	PrepareModuleData();

	// compilation
	if (CompileModule()) {
		m_changedCode = false;
		return true;
	}

	return false;
}

/**
 * Recompile
 * Purpose:
 * Translation and recompilation of the current source code into bytecode (object code)
 * Return value:
 * true,false
 */

bool ibCompileModule::Recompile()
{
	//clear functions & variables 
	Reset();

	if (m_parentModule != nullptr) {

		if (m_moduleObject != nullptr &&
			m_moduleObject->IsGlobalModule()) {

			m_strModuleName = m_moduleObject->GetFullName();
			m_strDocPath = m_moduleObject->GetDocPath();
			m_strFileName = m_moduleObject->GetFileName();

			m_changedCode = false;

			Load(m_moduleObject->GetModuleText());

			if (m_parentModule == nullptr)
				return true;
			// See ibCompileModule::Compile — a global is inlined into the root, and root->Compile()
			// Reset()s the root first, so re-driving it once the root is already built would wipe the
			// system-function table on any failure. Skip when the root is compiled.
			if (m_parentModule->m_cByteCode.m_bCompile)
				return true;
			return m_parentModule->Compile();
		}
	}

	if (m_moduleObject != nullptr) {

		m_cByteCode.m_strModuleName = m_moduleObject->GetFullName();

		if (m_parentModule) {
			m_cByteCode.m_parent = &m_parentModule->m_cByteCode;
			m_rootContext->m_parentContext = m_parentModule->m_rootContext;
		}

		m_strModuleName = m_moduleObject->GetFullName();
		m_strDocPath = m_moduleObject->GetDocPath();
		m_strFileName = m_moduleObject->GetFileName();

		Load(m_moduleObject->GetModuleText());
	}

	//prepare lexem 
	if (!PrepareLexem()) {
		return false;
	}

	//prepare context variables
	PrepareModuleData();

	// compilation 
	if (CompileModule()) {
		m_changedCode = false;
		return true;
	}

	m_changedCode = true;
	return false;
}

#pragma warning(pop)