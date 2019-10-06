// -*- mode: C++; c-file-style: "cc-mode" -*-
//*************************************************************************
// DESCRIPTION: Verilator: Emit C++ for tree
//
// Code available from: http://www.veripool.org/verilator
//
//*************************************************************************
//
// Copyright 2003-2019 by Wilson Snyder.  This program is free software; you can
// redistribute it and/or modify it under the terms of either the GNU
// Lesser General Public License Version 3 or the Perl Artistic License
// Version 2.0.
//
// Verilator is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
//*************************************************************************

#include "config_build.h"
#include "verilatedos.h"

#include "V3Global.h"
#include "V3EmitC.h"
#include "V3EmitCBase.h"
#include "V3LanguageWords.h"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <map>
#include <set>
#include <vector>

//######################################################################
// Symbol table emitting

class EmitCSyms : EmitCBaseVisitor {
    // NODE STATE
    // Cleared on Netlist
    //  AstNodeModule::user1()  -> bool.  Set true __Vconfigure called
    AstUser1InUse       m_inuser1;

    // TYPES
    struct ScopeData { string m_symName; string m_prettyName; string m_type;
        ScopeData(const string& symName, const string& prettyName, const string& type)
            : m_symName(symName), m_prettyName(prettyName), m_type(type) {}
    };
    struct ScopeFuncData { AstScopeName* m_scopep; AstCFunc* m_funcp; AstNodeModule* m_modp;
        ScopeFuncData(AstScopeName* scopep, AstCFunc* funcp, AstNodeModule* modp)
            : m_scopep(scopep), m_funcp(funcp), m_modp(modp) {}
    };
    struct ScopeVarData { string m_scopeName; string m_varBasePretty; AstVar* m_varp;
        AstNodeModule* m_modp;  AstScope* m_scopep;
        ScopeVarData(const string& scopeName, const string& varBasePretty,
                     AstVar* varp, AstNodeModule* modp, AstScope* scopep)
            : m_scopeName(scopeName), m_varBasePretty(varBasePretty)
            , m_varp(varp), m_modp(modp), m_scopep(scopep) {}
    };
    typedef std::map<string,ScopeFuncData> ScopeFuncs;
    typedef std::map<string,ScopeVarData> ScopeVars;
    typedef std::map<string,ScopeData> ScopeNames;
    typedef std::pair<AstScope*,AstNodeModule*> ScopeModPair;
    typedef std::pair<AstNodeModule*,AstVar*> ModVarPair;
    typedef std::vector<string> ScopeNameList;
    typedef std::map<string, ScopeNameList> ScopeNameHierarchy;
    struct CmpName {
        inline bool operator() (const ScopeModPair& lhsp, const ScopeModPair& rhsp) const {
            return lhsp.first->name() < rhsp.first->name();
        }
    };
    struct CmpDpi {
        inline bool operator() (const AstCFunc* lhsp, const AstCFunc* rhsp) const {
            if (lhsp->dpiImport() != rhsp->dpiImport()) {
                // cppcheck-suppress comparisonOfFuncReturningBoolError
                return lhsp->dpiImport() < rhsp->dpiImport();
            }
            return lhsp->name() < rhsp->name();
        }
    };

    // STATE
    AstCFunc*           m_funcp;        // Current function
    AstNodeModule*      m_modp;         // Current module
    std::vector<ScopeModPair> m_scopes;  // Every scope by module
    std::vector<AstCFunc*>    m_dpis;   // DPI functions
    std::vector<ModVarPair>   m_modVars;  // Each public {mod,var}
    ScopeNames          m_scopeNames;   // Each unique AstScopeName
    ScopeFuncs          m_scopeFuncs;   // Each {scope,dpi-export-func}
    ScopeVars           m_scopeVars;    // Each {scope,public-var}
    ScopeNames          m_vpiScopeCandidates;  // All scopes for VPI
    ScopeNameHierarchy  m_vpiScopeHierarchy;  // The actual hierarchy of scopes
    V3LanguageWords     m_words;        // Reserved word detector
    int         m_coverBins;            // Coverage bin number
    int         m_labelNum;             // Next label number
    bool        m_dpiHdrOnly;           // Only emit the DPI header
    int         m_numStmts;             // Number of statements output
    int         m_funcNum;              // CFunc split function number
    V3OutCFile* m_ofpBase;              // Base (not split) C file
    std::map<int,bool> m_usesVfinal;    // Split method uses __Vfinal

    // METHODS
    void emitSymHdr();
    void checkSplit(bool usesVfinal);
    void closeSplit();
    void emitSymImpPreamble();
    void emitSymImp();
    void emitDpiHdr();
    void emitDpiImp();

    void nameCheck(AstNode* nodep) {
        // Prevent GCC compile time error; name check all things that reach C++ code
        if (nodep->name() != "") {
            string rsvd = m_words.isKeyword(nodep->name());
            if (rsvd != "") {
                // Generally V3Name should find all of these and throw SYMRSVDWORD.
                // We'll still check here because the compiler errors
                // resulting if we miss this warning are SO nasty
                nodep->v3error("Symbol matching "+rsvd+" reserved word reached emitter,"
                               " should have hit SYMRSVDWORD: "<<nodep->prettyNameQ());
            }
        }
    }

    string scopeSymString(const string& scpname) {
        string out = scpname;
        string::size_type pos;
        while ((pos = out.find("__PVT__")) != string::npos) {
            out.replace(pos, 7, "");
        }
        if (out.substr(0, 10) == "TOP__DOT__") out.replace(0, 10, "");
        if (out.substr(0, 4) == "TOP.") out.replace(0, 4, "");
        while ((pos = out.find('.')) != string::npos) {
            out.replace(pos, 1, "__");
        }
        while ((pos = out.find("__DOT__")) != string::npos) {
            out.replace(pos, 7, "__");
        }
        return out;
    }

    string scopeDecodeIdentifier(const string& scpname) {
        string out = scpname;
        // Remove hierarchy
        string::size_type  pos = out.rfind(".");
        if (pos != std::string::npos) out.erase(0, pos + 1);
        // Decode all escaped characters
        while ((pos = out.find("__0")) != string::npos) {
            unsigned int x;
            std::stringstream ss;
            ss << std::hex << out.substr(pos+3, 2);
            ss >> x;
            out.replace(pos, 5, 1, (char) x);
        }
        return out;
    }

    void varHierarchyScopes(string scp) {
        while (!scp.empty()) {
            ScopeNames::const_iterator scpit = m_vpiScopeCandidates.find(scp);
            if ((scpit != m_vpiScopeCandidates.end())
                 && (m_scopeNames.find(scp) == m_scopeNames.end())) {
                m_scopeNames.insert(make_pair(scpit->second.m_symName, scpit->second));
            }
            string::size_type pos = scp.rfind("__DOT__");
            if (pos == string::npos) {
                pos = scp.rfind(".");
                if (pos == string::npos) {
                    break;
                }
            }
            scp.resize(pos);
        }
    }

    void varsExpand() {
        // We didn't have all m_scopes loaded when we encountered variables, so expand them now
        // It would be less code if each module inserted its own variables.
        // Someday.  For now public isn't common.
        for (std::vector<ScopeModPair>::iterator itsc = m_scopes.begin();
             itsc != m_scopes.end(); ++itsc) {
            AstScope* scopep = itsc->first;  AstNodeModule* smodp = itsc->second;
            for (std::vector<ModVarPair>::iterator it = m_modVars.begin();
                 it != m_modVars.end(); ++it) {
                AstNodeModule* modp = it->first;
                AstVar* varp = it->second;
                if (modp == smodp) {
                    // Need to split the module + var name into the
                    // original-ish full scope and variable name under that scope.
                    // The module instance name is included later, when we
                    // know the scopes this module is under
                    string whole = scopep->name()+"__DOT__"+varp->name();
                    string scpName;
                    string varBase;
                    if (whole.substr(0, 10) == "__DOT__TOP") whole.replace(0, 10, "");
                    string::size_type dpos = whole.rfind("__DOT__");
                    if (dpos != string::npos) {
                        scpName = whole.substr(0, dpos);
                        varBase = whole.substr(dpos+strlen("__DOT__"));
                    } else {
                        varBase = whole;
                    }
                    //UINFO(9,"For "<<scopep->name()<<" - "<<varp->name()<<"  Scp "<<scpName<<"  Var "<<varBase<<endl);
                    string varBasePretty = AstNode::prettyName(varBase);
                    string scpPretty = AstNode::prettyName(scpName);
                    string scpSym = scopeSymString(scpName);
                    //UINFO(9," scnameins sp "<<scpName<<" sp "<<scpPretty<<" ss "<<scpSym<<endl);
                    if (v3Global.opt.vpi()) {
                        varHierarchyScopes(scpName);
                    }
                    if (m_scopeNames.find(scpSym) == m_scopeNames.end()) {
                        m_scopeNames.insert(make_pair(scpSym, ScopeData(scpSym, scpPretty,
                                                      "SCOPE_OTHER")));
                    }
                    m_scopeVars.insert(
                        make_pair(scpSym + " " + varp->name(),
                                  ScopeVarData(scpSym, varBasePretty, varp, modp, scopep)));
                }
            }
        }
    }

    void buildVpiHierarchy() {
        for (ScopeNames::const_iterator it = m_scopeNames.begin(); it != m_scopeNames.end(); ++it) {
            if (it->second.m_type != "SCOPE_MODULE") continue;

            string name = it->second.m_prettyName;
            if (name.substr(0, 4) == "TOP.") name.replace(0, 4, "");

            string above = name;
            while (!above.empty()) {
                string::size_type pos = above.rfind(".");
                if (pos == string::npos) {
                    break;
                }
                above.resize(pos);
                if (m_vpiScopeHierarchy.find(above) != m_vpiScopeHierarchy.end()) {
                    m_vpiScopeHierarchy[above].push_back(name);
                    break;
                }
            }
            m_vpiScopeHierarchy[name] = std::vector<string>();
        }
    }

    // VISITORS
    virtual void visit(AstNetlist* nodep) {
        // Collect list of scopes
        iterateChildren(nodep);
        varsExpand();

        if (v3Global.opt.vpi()) {
            buildVpiHierarchy();
        }

        // Sort by names, so line/process order matters less
        stable_sort(m_scopes.begin(), m_scopes.end(), CmpName());
        stable_sort(m_dpis.begin(), m_dpis.end(), CmpDpi());

        // Output
        if (!m_dpiHdrOnly) {
            // Must emit implementation first to determine number of splits
            emitSymImp();
            emitSymHdr();
        }
        if (v3Global.dpi()) {
            emitDpiHdr();
            if (!m_dpiHdrOnly) emitDpiImp();
        }
    }
    virtual void visit(AstNodeModule* nodep) {
        nameCheck(nodep);
        m_modp = nodep;
        m_labelNum = 0;
        iterateChildren(nodep);
        m_modp = NULL;
    }
    virtual void visit(AstCellInline* nodep) {
        if (v3Global.opt.vpi()) {
            string type = (nodep->origModName() == "__BEGIN__") ? "SCOPE_OTHER"
                                                                : "SCOPE_MODULE";
            string name = nodep->scopep()->name() + "__DOT__" + nodep->name();
            string name_dedot = AstNode::dedotName(name);
            m_vpiScopeCandidates.insert(make_pair(name, ScopeData(scopeSymString(name),
                                                                  name_dedot, type)));
        }
    }
    virtual void visit(AstScope* nodep) {
        nameCheck(nodep);

        m_scopes.push_back(make_pair(nodep, m_modp));

        if (v3Global.opt.vpi() && !nodep->isTop()) {
            m_vpiScopeCandidates.insert(make_pair(nodep->name(),
                                                  ScopeData(scopeSymString(nodep->name()),
                                                            nodep->name(), "SCOPE_MODULE")));
        }
    }
    virtual void visit(AstScopeName* nodep) {
        string name = nodep->scopeSymName();
        //UINFO(9,"scnameins sp "<<nodep->name()<<" sp "<<nodep->scopePrettySymName()<<" ss "<<name<<endl);
        if (m_scopeNames.find(name) == m_scopeNames.end()) {
            m_scopeNames.insert(make_pair(name, ScopeData(name, nodep->scopePrettySymName(),
                                                          "SCOPE_OTHER")));
        }
        if (nodep->dpiExport()) {
            UASSERT_OBJ(m_funcp, nodep, "ScopeName not under DPI function");
            m_scopeFuncs.insert(make_pair(name + " " + m_funcp->name(),
                                          ScopeFuncData(nodep, m_funcp, m_modp)));
        } else {
            if (m_scopeNames.find(nodep->scopeDpiName()) == m_scopeNames.end()) {
                m_scopeNames.insert(make_pair(nodep->scopeDpiName(),
                                              ScopeData(nodep->scopeDpiName(),
                                                            nodep->scopePrettyDpiName(),
                                                            "SCOPE_OTHER")));
            }
        }
    }
    virtual void visit(AstVar* nodep) {
        nameCheck(nodep);
        iterateChildren(nodep);
        if (nodep->isSigUserRdPublic()
            && !nodep->isParam()) {  // The VPI functions require a pointer to allow modification, but parameters are constants
            m_modVars.push_back(make_pair(m_modp, nodep));
        }
    }
    virtual void visit(AstCoverDecl* nodep) {
        // Assign numbers to all bins, so we know how big of an array to use
        if (!nodep->dataDeclNullp()) {  // else duplicate we don't need code for
            nodep->binNum(m_coverBins++);
        }
    }
    virtual void visit(AstJumpLabel* nodep) {
        nodep->labelNum(++m_labelNum);
        iterateChildren(nodep);
    }
    virtual void visit(AstCFunc* nodep) {
        nameCheck(nodep);
        if (nodep->dpiImport() || nodep->dpiExportWrapper()) {
            m_dpis.push_back(nodep);
        }
        m_funcp = nodep;
        iterateChildren(nodep);
        m_funcp = NULL;
    }
    // NOPs
    virtual void visit(AstConst*) {}
    // Default
    virtual void visit(AstNode* nodep) {
        iterateChildren(nodep);
    }
    //---------------------------------------
    // ACCESSORS
public:
    explicit EmitCSyms(AstNetlist* nodep, bool dpiHdrOnly):
        m_dpiHdrOnly(dpiHdrOnly)
    {
        m_funcp = NULL;
        m_modp = NULL;
        m_coverBins = 0;
        m_labelNum = 0;
        m_numStmts = 0;
        m_funcNum = 0;
        m_ofpBase = NULL;
        iterate(nodep);
    }
};

void EmitCSyms::emitSymHdr() {
    UINFO(6,__FUNCTION__<<": "<<endl);
    string filename = v3Global.opt.makeDir()+"/"+symClassName()+".h";
    newCFile(filename, true/*slow*/, false/*source*/);
    V3OutCFile hf (filename);
    m_ofp = &hf;

    ofp()->putsHeader();
    puts("// DESCR" "IPTION: Verilator output: Symbol table internal header\n");
    puts("//\n");
    puts("// Internal details; most calling programs do not need this header,\n");
    puts("// unless using verilator public meta comments.\n");
    puts("\n");

    puts("#ifndef _"+symClassName()+"_H_\n");
    puts("#define _"+symClassName()+"_H_\n");
    puts("\n");

    ofp()->putsIntTopInclude();
    if (v3Global.needHeavy()) {
        puts("#include \"verilated_heavy.h\"\n");
    } else {
        puts("#include \"verilated.h\"\n");
    }

    // for
    puts("\n// INCLUDE MODULE CLASSES\n");
    for (AstNodeModule* nodep = v3Global.rootp()->modulesp();
         nodep; nodep=VN_CAST(nodep->nextp(), NodeModule)) {
        puts("#include \""+modClassName(nodep)+".h\"\n");
    }

    if (v3Global.dpi()) {
        puts("\n// DPI TYPES for DPI Export callbacks (Internal use)\n");
        std::map<string,int> types;  // Remove duplicates and sort
        for (ScopeFuncs::iterator it = m_scopeFuncs.begin(); it != m_scopeFuncs.end(); ++it) {
            AstCFunc* funcp = it->second.m_funcp;
            if (funcp->dpiExport()) {
                string cbtype = v3Global.opt.prefix()+"__Vcb_"+funcp->cname()+"_t";
                types["typedef void (*"+cbtype+") ("+cFuncArgs(funcp)+");\n"] = 1;
            }
        }
        for (std::map<string,int>::iterator it = types.begin(); it != types.end(); ++it) {
            puts(it->first);
        }
    }

    puts("\n// SYMS CLASS\n");
    puts(string("class ")+symClassName()+" : public VerilatedSyms {\n");
    ofp()->putsPrivate(false);  // public:

    puts("\n// LOCAL STATE\n");
    puts("const char* __Vm_namep;\n");  // Must be before subcells, as constructor order needed before _vlCoverInsert.
    if (v3Global.opt.trace()) {
        puts("bool __Vm_activity;  ///< Used by trace routines to determine change occurred\n");
    }
    puts("bool __Vm_didInit;\n");

    puts("\n// SUBCELL STATE\n");
    for (std::vector<ScopeModPair>::iterator it = m_scopes.begin(); it != m_scopes.end(); ++it) {
        AstScope* scopep = it->first;  AstNodeModule* modp = it->second;
        if (modp->isTop()) {
            ofp()->printf("%-30s ", (modClassName(modp)+"*").c_str());
            puts(scopep->nameDotless()+"p;\n");
        }
        else {
            ofp()->printf("%-30s ", (modClassName(modp)+"").c_str());
            puts(scopep->nameDotless()+";\n");
        }
    }

    if (m_coverBins) {
        puts("\n// COVERAGE\n");
        puts("uint32_t __Vcoverage["); puts(cvtToStr(m_coverBins)); puts("];\n");
    }

    if (!m_scopeNames.empty()) {  // Scope names
        puts("\n// SCOPE NAMES\n");
        for (ScopeNames::iterator it = m_scopeNames.begin(); it != m_scopeNames.end(); ++it) {
            puts("VerilatedScope __Vscope_"+it->second.m_symName+";\n");
        }
    }

    if (v3Global.opt.vpi()) {
        puts("\n// SCOPE HIERARCHY\n");
        puts("VerilatedHierarchy __Vhier;\n");
    }

    puts("\n// CREATORS\n");
    puts(symClassName()+"("+topClassName()+"* topp, const char* namep);\n");
    puts(string("~")+symClassName()+"() {}\n");

    for (std::map<int,bool>::iterator it = m_usesVfinal.begin();
         it != m_usesVfinal.end(); ++it) {
        puts("void "+symClassName()+"_"+cvtToStr(it->first)+"(");
        if (it->second) {
            puts("int __Vfinal");
        } else {
            puts(topClassName()+"* topp");
        }
        puts(");\n");
    }

    puts("\n// METHODS\n");
    puts("inline const char* name() { return __Vm_namep; }\n");
    if (v3Global.opt.trace()) {
        puts("inline bool getClearActivity() { bool r=__Vm_activity; __Vm_activity=false; return r; }\n");
    }
    if (v3Global.opt.savable() ) {
        puts("void __Vserialize(VerilatedSerialize& os);\n");
        puts("void __Vdeserialize(VerilatedDeserialize& os);\n");
    }
    puts("\n");
    puts("} VL_ATTR_ALIGNED(64);\n");
    puts("\n");
    puts("#endif  // guard\n");
}

void EmitCSyms::closeSplit() {
    if (!m_ofp || m_ofp == m_ofpBase) return;

    puts("}\n");
    delete m_ofp;
    m_ofp = NULL;
}

void EmitCSyms::checkSplit(bool usesVfinal) {
    if (m_ofp && (!v3Global.opt.outputSplitCFuncs() ||
                  m_numStmts < v3Global.opt.outputSplitCFuncs())) return;

    m_numStmts = 0;
    string filename = v3Global.opt.makeDir()+"/"+symClassName()+"__"+cvtToStr(++m_funcNum)+".cpp";
    AstCFile* cfilep = newCFile(filename, true/*slow*/, true/*source*/);
    cfilep->support(true);
    m_usesVfinal[m_funcNum] = usesVfinal;
    closeSplit();

    m_ofp = new V3OutCFile(filename);

    m_ofpBase->puts(symClassName()+"_"+cvtToStr(m_funcNum)+"(");
    if (usesVfinal) {
        m_ofpBase->puts("__Vfinal");
    } else {
        m_ofpBase->puts("topp");
    }
    m_ofpBase->puts(");\n");

    emitSymImpPreamble();
    puts("void "+symClassName()+"::"+symClassName()+"_"+cvtToStr(m_funcNum)+"(");
    if (usesVfinal) {
        puts("int __Vfinal");
    } else {
        puts(topClassName()+"* topp");
    }
    puts(") {\n");
}

void EmitCSyms::emitSymImpPreamble() {
    ofp()->putsHeader();
    puts("// DESCR" "IPTION: Verilator output: Symbol table implementation internals\n");
    puts("\n");

    // Includes
    puts("#include \""+symClassName()+".h\"\n");
    for (AstNodeModule* nodep = v3Global.rootp()->modulesp();
         nodep; nodep=VN_CAST(nodep->nextp(), NodeModule)) {
        puts("#include \""+modClassName(nodep)+".h\"\n");
    }
}

void EmitCSyms::emitSymImp() {
    UINFO(6,__FUNCTION__<<": "<<endl);
    string filename = v3Global.opt.makeDir()+"/"+symClassName()+".cpp";
    AstCFile* cfilep = newCFile(filename, true/*slow*/, true/*source*/);
    cfilep->support(true);
    V3OutCFile cf (filename);
    m_ofp = &cf;
    m_ofpBase = m_ofp;
    emitSymImpPreamble();

    //puts("\n// GLOBALS\n");

    puts("\n");

    if (v3Global.opt.savable() ) {
        puts("\n");
        for (int de=0; de<2; ++de) {
            string classname = de ? "VerilatedDeserialize" : "VerilatedSerialize";
            string funcname = de ? "__Vdeserialize" : "__Vserialize";
            string op = de ? ">>" : "<<";
            // NOLINTNEXTLINE(performance-inefficient-string-concatenation)
            puts("void "+symClassName()+"::"+funcname+"("+classname+"& os) {\n");
            puts(   "// LOCAL STATE\n");
            // __Vm_namep presumably already correct
            if (v3Global.opt.trace()) {
                puts(   "os"+op+"__Vm_activity;\n");
            }
            puts(   "os"+op+"__Vm_didInit;\n");
            puts(   "// SUBCELL STATE\n");
            for (std::vector<ScopeModPair>::iterator it = m_scopes.begin();
                 it != m_scopes.end(); ++it) {
                AstScope* scopep = it->first;  AstNodeModule* modp = it->second;
                if (!modp->isTop()) {
                    puts(   scopep->nameDotless()+"."+funcname+"(os);\n");
                }
            }
            puts("}\n");
        }
    }

    puts("\n");

    puts("\n// FUNCTIONS\n");
    puts(symClassName()+"::"+symClassName()+"("+topClassName()+"* topp, const char* namep)\n");
    puts("    // Setup locals\n");
    puts("    : __Vm_namep(namep)\n");  // No leak, as we get destroyed when the top is destroyed
    if (v3Global.opt.trace()) {
        puts("    , __Vm_activity(false)\n");
    }
    puts("    , __Vm_didInit(false)\n");
    puts("    // Setup submodule names\n");
    char comma=',';
    for (std::vector<ScopeModPair>::iterator it = m_scopes.begin(); it != m_scopes.end(); ++it) {
        AstScope* scopep = it->first;  AstNodeModule* modp = it->second;
        if (modp->isTop()) {
        } else {
            puts(string("    ")+comma+" "+scopep->nameDotless());
            puts("(Verilated::catName(topp->name(),");
            // The "." is added by catName
            putsQuoted(scopep->prettyName());
            puts("))\n");
            comma = ',';
            ++m_numStmts;
        }
    }
    puts("{\n");

    puts("// Pointer to top level\n");
    puts("TOPp = topp;\n");
    puts("// Setup each module's pointers to their submodules\n");
    for (std::vector<ScopeModPair>::iterator it = m_scopes.begin(); it != m_scopes.end(); ++it) {
        AstScope* scopep = it->first;  AstNodeModule* modp = it->second;
        if (!modp->isTop()) {
            checkSplit(false);
            string arrow = scopep->name();
            string::size_type pos;
            while ((pos = arrow.find('.')) != string::npos) {
                arrow.replace(pos, 1, "->");
            }
            if (arrow.substr(0, 5) == "TOP->") arrow.replace(0, 5, "TOPp->");
            ofp()->printf("%-30s ", arrow.c_str());
            puts(" = &");
            puts(scopep->nameDotless()+";\n");
            ++m_numStmts;
        }
    }

    puts("// Setup each module's pointer back to symbol table (for public functions)\n");
    puts("TOPp->__Vconfigure(this, true);\n");
    for (std::vector<ScopeModPair>::iterator it = m_scopes.begin(); it != m_scopes.end(); ++it) {
        AstScope* scopep = it->first;  AstNodeModule* modp = it->second;
        if (!modp->isTop()) {
            checkSplit(false);
            // first is used by AstCoverDecl's call to __vlCoverInsert
            bool first = !modp->user1();
            modp->user1(true);
            puts(scopep->nameDotless()+".__Vconfigure(this, "
                 +(first?"true":"false")
                 +");\n");
            ++m_numStmts;
        }
    }

    if (!m_scopeNames.empty()) {  // Setup scope names
        puts("// Setup scopes\n");
        for (ScopeNames::iterator it = m_scopeNames.begin(); it != m_scopeNames.end(); ++it) {
            checkSplit(false);
            puts("__Vscope_"+it->second.m_symName+".configure(this,name(),");
            putsQuoted(it->second.m_prettyName);
            puts(", ");
            putsQuoted(scopeDecodeIdentifier(it->second.m_prettyName));
            puts(", VerilatedScope::"+it->second.m_type+");\n");
            ++m_numStmts;
        }
    }

    if (v3Global.opt.vpi()) {
        puts("\n// Setup scope hierarchy\n");
        for (ScopeNames::const_iterator it = m_scopeNames.begin();
            it != m_scopeNames.end(); ++it) {
            string name = it->second.m_prettyName;
            if (it->first == "TOP") continue;
            name = name.replace(0, 4, "");  // Remove the "TOP."
            if ((name.find(".") == string::npos) && (it->second.m_type == "SCOPE_MODULE")) {
                puts("__Vhier.add(0, &__Vscope_" + it->second.m_symName + ");\n");
            }
        }

        for (ScopeNameHierarchy::const_iterator it = m_vpiScopeHierarchy.begin();
             it != m_vpiScopeHierarchy.end(); ++it) {
            for (ScopeNameList::const_iterator lit = it->second.begin();
                lit != it->second.end(); ++lit) {
                string fromname = scopeSymString(it->first);
                string toname = scopeSymString(*lit);
                ScopeNames::const_iterator from = m_scopeNames.find(fromname);
                ScopeNames::const_iterator to = m_scopeNames.find(toname);
                UASSERT(from != m_scopeNames.end(), fromname+" not in m_scopeNames");
                UASSERT(to != m_scopeNames.end(), toname+" not in m_scopeNames");
                puts("__Vhier.add(");
                puts("&__Vscope_"+from->second.m_symName+", ");
                puts("&__Vscope_"+to->second.m_symName+");\n");
            }
        }
        puts("\n");
    }

    // Everything past here is in the __Vfinal loop, so start a new split file if needed
    closeSplit();

    if (v3Global.dpi()) {
        m_ofpBase->puts("// Setup export functions\n");
        m_ofpBase->puts("for (int __Vfinal=0; __Vfinal<2; __Vfinal++) {\n");
        for (ScopeFuncs::iterator it = m_scopeFuncs.begin(); it != m_scopeFuncs.end(); ++it) {
            AstScopeName* scopep = it->second.m_scopep;
            AstCFunc* funcp = it->second.m_funcp;
            AstNodeModule* modp = it->second.m_modp;
            if (funcp->dpiExport()) {
                checkSplit(true);
                puts("__Vscope_"+scopep->scopeSymName()+".exportInsert(__Vfinal,");
                putsQuoted(funcp->cname());
                puts(", (void*)(&");
                puts(modClassName(modp));
                puts("::");
                puts(funcp->name());
                puts("));\n");
                ++m_numStmts;
            }
        }
        // It would be less code if each module inserted its own variables.
        // Someday.  For now public isn't common.
        for (ScopeVars::iterator it = m_scopeVars.begin(); it != m_scopeVars.end(); ++it) {
            checkSplit(true);
            AstNodeModule* modp = it->second.m_modp;
            AstScope* scopep = it->second.m_scopep;
            AstVar* varp = it->second.m_varp;
            //
            int pdim = 0;
            int udim = 0;
            string bounds;
            if (AstBasicDType* basicp = varp->basicp()) {
                // Range is always first, it's not in "C" order
                if (basicp->isRanged()) {
                    bounds += " ,"; bounds += cvtToStr(basicp->msb());
                    bounds += ","; bounds += cvtToStr(basicp->lsb());
                    pdim++;
                }
                for (AstNodeDType* dtypep = varp->dtypep(); dtypep; ) {
                    dtypep = dtypep->skipRefp();  // Skip AstRefDType/AstTypedef, or return same node
                    if (const AstNodeArrayDType* adtypep = VN_CAST(dtypep, NodeArrayDType)) {
                        bounds += " ,"; bounds += cvtToStr(adtypep->msb());
                        bounds += ","; bounds += cvtToStr(adtypep->lsb());
                        if (VN_IS(dtypep, PackArrayDType)) pdim++; else udim++;
                        dtypep = adtypep->subDTypep();
                    }
                    else break;  // AstBasicDType - nothing below, 1
                }
            }
            //
            if (pdim>1 || udim>1) {
                puts("//UNSUP ");  // VerilatedImp can't deal with >2d or packed arrays
            }
            puts("__Vscope_"+it->second.m_scopeName+".varInsert(__Vfinal,");
            putsQuoted(it->second.m_varBasePretty);
            puts(", &(");
            if (modp->isTop()) {
                puts(scopep->nameDotless());
                puts("p->");
            } else {
                puts(scopep->nameDotless());
                puts(".");
            }
            puts(varp->name());
            puts("), ");
            puts(varp->vlEnumType());  // VLVT_UINT32 etc
            puts(",");
            puts(varp->vlEnumDir());  // VLVD_IN etc
            puts(",");
            puts(cvtToStr(pdim+udim));
            puts(bounds);
            puts(");\n");
            ++m_numStmts;
        }
        m_ofpBase->puts("}\n");
    }

    m_ofpBase->puts("}\n");
    closeSplit();
}

//######################################################################

void EmitCSyms::emitDpiHdr() {
    UINFO(6,__FUNCTION__<<": "<<endl);
    string filename = v3Global.opt.makeDir()+"/"+topClassName()+"__Dpi.h";
    AstCFile* cfilep = newCFile(filename, false/*slow*/, false/*source*/);
    cfilep->support(true);
    V3OutCFile hf (filename);
    m_ofp = &hf;

    m_ofp->putsHeader();
    puts("// DESCR" "IPTION: Verilator output: Prototypes for DPI import and export functions.\n");
    puts("//\n");
    puts("// Verilator includes this file in all generated .cpp files that use DPI functions.\n");
    puts("// Manually include this file where DPI .c import functions are declared to insure\n");
    puts("// the C functions match the expectations of the DPI imports.\n");
    puts("\n");
    puts("#include \"svdpi.h\"\n");
    puts("\n");
    puts("#ifdef __cplusplus\n");
    puts("extern \"C\" {\n");
    puts("#endif\n");
    puts("\n");

    int firstExp = 0;
    int firstImp = 0;
    for (std::vector<AstCFunc*>::iterator it = m_dpis.begin(); it != m_dpis.end(); ++it) {
        AstCFunc* nodep = *it;
        if (nodep->dpiExportWrapper()) {
            if (!firstExp++) puts("\n// DPI EXPORTS\n");
            puts("// DPI export at "+nodep->fileline()->ascii()+"\n");
            puts("extern "+nodep->rtnTypeVoid()+" "+nodep->name()+"("+cFuncArgs(nodep)+");\n");
        }
        else if (nodep->dpiImport()) {
            if (!firstImp++) puts("\n// DPI IMPORTS\n");
            puts("// DPI import at "+nodep->fileline()->ascii()+"\n");
            puts("extern "+nodep->rtnTypeVoid()+" "+nodep->name()+"("+cFuncArgs(nodep)+");\n");
        }
    }

    puts("\n");
    puts("#ifdef __cplusplus\n");
    puts("}\n");
    puts("#endif\n");
}

//######################################################################

void EmitCSyms::emitDpiImp() {
    UINFO(6,__FUNCTION__<<": "<<endl);
    string filename = v3Global.opt.makeDir()+"/"+topClassName()+"__Dpi.cpp";
    AstCFile* cfilep = newCFile(filename, false/*slow*/, true/*source*/);
    cfilep->support(true);
    V3OutCFile hf (filename);
    m_ofp = &hf;

    m_ofp->putsHeader();
    puts("// DESCR" "IPTION: Verilator output: Implementation of DPI export functions.\n");
    puts("//\n");
    puts("// Verilator compiles this file in when DPI functions are used.\n");
    puts("// If you have multiple Verilated designs with the same DPI exported\n");
    puts("// function names, you will get multiple definition link errors from here.\n");
    puts("// This is an unfortunate result of the DPI specification.\n");
    puts("// To solve this, either\n");
    puts("//    1. Call "+topClassName()+"::{export_function} instead,\n");
    puts("//       and do not even bother to compile this file\n");
    puts("// or 2. Compile all __Dpi.cpp files in the same compiler run,\n");
    puts("//       and #ifdefs already inserted here will sort everything out.\n");
    puts("\n");

    puts("#include \""+topClassName()+"__Dpi.h\"\n");
    puts("#include \""+topClassName()+".h\"\n");
    puts("\n");

    for (std::vector<AstCFunc*>::iterator it = m_dpis.begin(); it != m_dpis.end(); ++it) {
        AstCFunc* nodep = *it;
        if (nodep->dpiExportWrapper()) {
            puts("#ifndef _VL_DPIDECL_"+nodep->name()+"\n");
            puts("#define _VL_DPIDECL_"+nodep->name()+"\n");
            puts(nodep->rtnTypeVoid()+" "+nodep->name()+"("+cFuncArgs(nodep)+") {\n");
            puts("// DPI Export at "+nodep->fileline()->ascii()+"\n");
            puts("return "+topClassName()+"::"+nodep->name()+"(");
            string args;
            for (AstNode* stmtp = nodep->argsp(); stmtp; stmtp=stmtp->nextp()) {
                if (const AstVar* portp = VN_CAST(stmtp, Var)) {
                    if (portp->isIO() && !portp->isFuncReturn()) {
                        if (args != "") args+= ", ";
                        args += portp->name();
                    }
                }
            }
            puts(args+");\n");
            puts("}\n");
            puts("#endif\n");
            puts("\n");
        }
    }
}

//######################################################################
// EmitC class functions

void V3EmitC::emitcSyms(bool dpiHdrOnly) {
    UINFO(2,__FUNCTION__<<": "<<endl);
    EmitCSyms syms (v3Global.rootp(), dpiHdrOnly);
}
