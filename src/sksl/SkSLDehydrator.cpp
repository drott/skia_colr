/*
 * Copyright 2020 Google LLC
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "src/sksl/SkSLDehydrator.h"

#include <map>

#include "src/sksl/SkSLRehydrator.h"
#include "src/sksl/ir/SkSLBinaryExpression.h"
#include "src/sksl/ir/SkSLBreakStatement.h"
#include "src/sksl/ir/SkSLConstructor.h"
#include "src/sksl/ir/SkSLContinueStatement.h"
#include "src/sksl/ir/SkSLDiscardStatement.h"
#include "src/sksl/ir/SkSLDoStatement.h"
#include "src/sksl/ir/SkSLEnum.h"
#include "src/sksl/ir/SkSLExpressionStatement.h"
#include "src/sksl/ir/SkSLField.h"
#include "src/sksl/ir/SkSLFieldAccess.h"
#include "src/sksl/ir/SkSLForStatement.h"
#include "src/sksl/ir/SkSLFunctionCall.h"
#include "src/sksl/ir/SkSLFunctionDeclaration.h"
#include "src/sksl/ir/SkSLFunctionDefinition.h"
#include "src/sksl/ir/SkSLIfStatement.h"
#include "src/sksl/ir/SkSLIndexExpression.h"
#include "src/sksl/ir/SkSLInlineMarker.h"
#include "src/sksl/ir/SkSLIntLiteral.h"
#include "src/sksl/ir/SkSLInterfaceBlock.h"
#include "src/sksl/ir/SkSLNullLiteral.h"
#include "src/sksl/ir/SkSLPostfixExpression.h"
#include "src/sksl/ir/SkSLPrefixExpression.h"
#include "src/sksl/ir/SkSLProgramElement.h"
#include "src/sksl/ir/SkSLReturnStatement.h"
#include "src/sksl/ir/SkSLSetting.h"
#include "src/sksl/ir/SkSLStatement.h"
#include "src/sksl/ir/SkSLSwitchCase.h"
#include "src/sksl/ir/SkSLSwitchStatement.h"
#include "src/sksl/ir/SkSLSwizzle.h"
#include "src/sksl/ir/SkSLSymbol.h"
#include "src/sksl/ir/SkSLSymbolAlias.h"
#include "src/sksl/ir/SkSLSymbolTable.h"
#include "src/sksl/ir/SkSLTernaryExpression.h"
#include "src/sksl/ir/SkSLUnresolvedFunction.h"
#include "src/sksl/ir/SkSLVarDeclarations.h"
#include "src/sksl/ir/SkSLVariable.h"
#include "src/sksl/ir/SkSLWhileStatement.h"

#ifdef SKSL_STANDALONE

namespace SkSL {

static constexpr int HEADER_SIZE = 2;

class AutoDehydratorSymbolTable {
public:
    AutoDehydratorSymbolTable(Dehydrator* dehydrator, const std::shared_ptr<SymbolTable>& symbols)
        : fDehydrator(dehydrator) {
        dehydrator->fSymbolMap.emplace_back();
        if (symbols) {
            dehydrator->write(*symbols);
        } else {
            dehydrator->writeCommand(Rehydrator::kVoid_Command);
        }
    }

    ~AutoDehydratorSymbolTable() {
        fDehydrator->fSymbolMap.pop_back();
    }

private:
    Dehydrator* fDehydrator;
};

void Dehydrator::write(Layout l) {
    if (l == Layout()) {
        this->writeCommand(Rehydrator::kDefaultLayout_Command);
    } else if (l == Layout::builtin(l.fBuiltin)) {
        this->writeCommand(Rehydrator::kBuiltinLayout_Command);
        this->writeS16(l.fBuiltin);
    } else {
        this->writeCommand(Rehydrator::kLayout_Command);
        fBody.write32(l.fFlags);
        this->writeS8(l.fLocation);
        this->writeS8(l.fOffset);
        this->writeS8(l.fBinding);
        this->writeS8(l.fIndex);
        this->writeS8(l.fSet);
        this->writeS16(l.fBuiltin);
        this->writeS8(l.fInputAttachmentIndex);
        this->writeS8((int) l.fFormat);
        this->writeS8(l.fPrimitive);
        this->writeS8(l.fMaxVertices);
        this->writeS8(l.fInvocations);
        this->write(l.fMarker);
        this->write(l.fWhen);
        this->writeS8(l.fKey);
        this->writeS8((int) l.fCType);
    }
}

void Dehydrator::write(Modifiers m) {
    if (m == Modifiers()) {
        this->writeCommand(Rehydrator::kDefaultModifiers_Command);
    } else {
        if (m.fFlags <= 255) {
            this->writeCommand(Rehydrator::kModifiers8Bit_Command);
            this->write(m.fLayout);
            this->writeU8(m.fFlags);
        } else {
            this->writeCommand(Rehydrator::kModifiers_Command);
            this->write(m.fLayout);
            this->writeS32(m.fFlags);
        }
    }
}

void Dehydrator::write(StringFragment s) {
    this->write(String(s));
}

void Dehydrator::write(String s) {
    auto found = fStrings.find(s);
    int offset;
    if (found == fStrings.end()) {
        offset = fStringBuffer.str().length() + HEADER_SIZE;
        fStrings.insert({ s, offset });
        SkASSERT(s.length() <= 255);
        fStringBreaks.add(fStringBuffer.bytesWritten());
        fStringBuffer.write8(s.length());
        fStringBuffer.writeString(s);
    } else {
        offset = found->second;
    }
    this->writeU16(offset);
}

void Dehydrator::write(const Symbol& s) {
    uint16_t id = this->symbolId(&s, false);
    if (id) {
        this->writeCommand(Rehydrator::kSymbolRef_Command);
        this->writeU16(id);
        return;
    }
    switch (s.kind()) {
        case Symbol::Kind::kFunctionDeclaration: {
            const FunctionDeclaration& f = s.as<FunctionDeclaration>();
            this->writeCommand(Rehydrator::kFunctionDeclaration_Command);
            this->writeId(&f);
            this->write(f.modifiers());
            this->write(f.name());
            this->writeU8(f.parameters().size());
            for (const Variable* p : f.parameters()) {
                this->writeU16(this->symbolId(p));
            }
            this->write(f.returnType());
            break;
        }
        case Symbol::Kind::kSymbolAlias: {
            const SymbolAlias& alias = s.as<SymbolAlias>();
            this->writeCommand(Rehydrator::kSymbolAlias_Command);
            this->writeId(&alias);
            this->write(alias.name());
            this->write(*alias.origSymbol());
            break;
        }
        case Symbol::Kind::kUnresolvedFunction: {
            const UnresolvedFunction& f = s.as<UnresolvedFunction>();
            this->writeCommand(Rehydrator::kUnresolvedFunction_Command);
            this->writeId(&f);
            this->writeU8(f.fFunctions.size());
            for (const FunctionDeclaration* funcDecl : f.fFunctions) {
                this->write(*funcDecl);
            }
            break;
        }
        case Symbol::Kind::kType: {
            const Type& t = s.as<Type>();
            switch (t.typeKind()) {
                case Type::TypeKind::kArray:
                    this->writeCommand(Rehydrator::kArrayType_Command);
                    this->writeId(&t);
                    this->write(t.componentType());
                    this->writeS8(t.columns());
                    break;
                case Type::TypeKind::kEnum:
                    this->writeCommand(Rehydrator::kEnumType_Command);
                    this->writeId(&t);
                    this->write(t.name());
                    break;
                case Type::TypeKind::kNullable:
                    this->writeCommand(Rehydrator::kNullableType_Command);
                    this->writeId(&t);
                    this->write(t.componentType());
                    break;
                case Type::TypeKind::kStruct:
                    this->writeCommand(Rehydrator::kStructType_Command);
                    this->writeId(&t);
                    this->write(t.name());
                    this->writeU8(t.fields().size());
                    for (const Type::Field& f : t.fields()) {
                        this->write(f.fModifiers);
                        this->write(f.fName);
                        this->write(*f.fType);
                    }
                    break;
                default:
                    this->writeCommand(Rehydrator::kSystemType_Command);
                    this->writeId(&t);
                    this->write(t.name());
            }
            break;
        }
        case Symbol::Kind::kVariable: {
            const Variable& v = s.as<Variable>();
            this->writeCommand(Rehydrator::kVariable_Command);
            this->writeId(&v);
            this->write(v.modifiers());
            this->write(v.name());
            this->write(v.type());
            this->writeU8(v.storage());
            break;
        }
        case Symbol::Kind::kField: {
            const Field& f = s.as<Field>();
            this->writeCommand(Rehydrator::kField_Command);
            this->writeU16(this->symbolId(&f.owner()));
            this->writeU8(f.fieldIndex());
            break;
        }
        case Symbol::Kind::kExternal:
            SkASSERT(false);
            break;
    }
}

void Dehydrator::write(const SymbolTable& symbols) {
    this->writeCommand(Rehydrator::kSymbolTable_Command);
    this->writeU16(symbols.fOwnedSymbols.size());
    for (const std::unique_ptr<const Symbol>& s : symbols.fOwnedSymbols) {
        this->write(*s);
    }
    this->writeU16(symbols.fSymbols.size());
    std::map<StringFragment, const Symbol*> ordered;
    for (std::pair<StringFragment, const Symbol*> p : symbols.fSymbols) {
        ordered.insert(p);
    }
    for (std::pair<StringFragment, const Symbol*> p : ordered) {
        bool found = false;
        for (size_t i = 0; i < symbols.fOwnedSymbols.size(); ++i) {
            if (symbols.fOwnedSymbols[i].get() == p.second) {
                fCommandBreaks.add(fBody.bytesWritten());
                this->writeU16(i);
                found = true;
                break;
            }
        }
        SkASSERT(found);
    }
}

void Dehydrator::write(const Expression* e) {
    if (e) {
        switch (e->kind()) {
            case Expression::Kind::kBinary: {
                const BinaryExpression& b = e->as<BinaryExpression>();
                this->writeCommand(Rehydrator::kBinary_Command);
                this->write(&b.left());
                this->writeU8((int) b.getOperator());
                this->write(&b.right());
                this->write(b.type());
                break;
            }
            case Expression::Kind::kBoolLiteral: {
                const BoolLiteral& b = e->as<BoolLiteral>();
                this->writeCommand(Rehydrator::kBoolLiteral_Command);
                this->writeU8(b.value());
                break;
            }
            case Expression::Kind::kConstructor: {
                const Constructor& c = e->as<Constructor>();
                this->writeCommand(Rehydrator::kConstructor_Command);
                this->write(c.type());
                this->writeU8(c.arguments().size());
                for (const auto& a : c.arguments()) {
                    this->write(a.get());
                }
                break;
            }
            case Expression::Kind::kExternalFunctionCall:
            case Expression::Kind::kExternalValue:
                SkDEBUGFAIL("unimplemented--not expected to be used from within an include file");
                break;
            case Expression::Kind::kFieldAccess: {
                const FieldAccess& f = e->as<FieldAccess>();
                this->writeCommand(Rehydrator::kFieldAccess_Command);
                this->write(f.fBase.get());
                this->writeU8(f.fFieldIndex);
                this->writeU8(f.fOwnerKind);
                break;
            }
            case Expression::Kind::kFloatLiteral: {
                const FloatLiteral& f = e->as<FloatLiteral>();
                this->writeCommand(Rehydrator::kFloatLiteral_Command);
                FloatIntUnion u;
                u.fFloat = f.value();
                this->writeS32(u.fInt);
                break;
            }
            case Expression::Kind::kFunctionCall: {
                const FunctionCall& f = e->as<FunctionCall>();
                this->writeCommand(Rehydrator::kFunctionCall_Command);
                this->write(f.type());
                this->writeId(&f.function());
                this->writeU8(f.arguments().size());
                for (const auto& a : f.arguments()) {
                    this->write(a.get());
                }
                break;
            }
            case Expression::Kind::kIndex: {
                const IndexExpression& i = e->as<IndexExpression>();
                this->writeCommand(Rehydrator::kIndex_Command);
                this->write(i.fBase.get());
                this->write(i.fIndex.get());
                break;
            }
            case Expression::Kind::kIntLiteral: {
                const IntLiteral& i = e->as<IntLiteral>();
                this->writeCommand(Rehydrator::kIntLiteral_Command);
                this->writeS32(i.value());
                break;
            }
            case Expression::Kind::kNullLiteral:
                this->writeCommand(Rehydrator::kNullLiteral_Command);
                break;
            case Expression::Kind::kPostfix: {
                const PostfixExpression& p = e->as<PostfixExpression>();
                this->writeCommand(Rehydrator::kPostfix_Command);
                this->writeU8((int) p.fOperator);
                this->write(p.fOperand.get());
                break;
            }
            case Expression::Kind::kPrefix: {
                const PrefixExpression& p = e->as<PrefixExpression>();
                this->writeCommand(Rehydrator::kPrefix_Command);
                this->writeU8((int) p.fOperator);
                this->write(p.fOperand.get());
                break;
            }
            case Expression::Kind::kSetting: {
                const Setting& s = e->as<Setting>();
                this->writeCommand(Rehydrator::kSetting_Command);
                this->write(s.name());
                this->write(s.type());
                break;
            }
            case Expression::Kind::kSwizzle: {
                const Swizzle& s = e->as<Swizzle>();
                this->writeCommand(Rehydrator::kSwizzle_Command);
                this->write(s.fBase.get());
                this->writeU8(s.fComponents.size());
                for (int c : s.fComponents) {
                    this->writeU8(c);
                }
                break;
            }
            case Expression::Kind::kTernary: {
                const TernaryExpression& t = e->as<TernaryExpression>();
                this->writeCommand(Rehydrator::kTernary_Command);
                this->write(t.test().get());
                this->write(t.ifTrue().get());
                this->write(t.ifFalse().get());
                break;
            }
            case Expression::Kind::kVariableReference: {
                const VariableReference& v = e->as<VariableReference>();
                this->writeCommand(Rehydrator::kVariableReference_Command);
                this->writeId(v.variable());
                this->writeU8(v.refKind());
                break;
            }
            case Expression::Kind::kFunctionReference:
            case Expression::Kind::kTypeReference:
            case Expression::Kind::kDefined:
                SkDEBUGFAIL("this expression shouldn't appear in finished code");
                break;
        }
    } else {
        this->writeCommand(Rehydrator::kVoid_Command);
    }
}

void Dehydrator::write(const Statement* s) {
    if (s) {
        switch (s->kind()) {
            case Statement::Kind::kBlock: {
                const Block& b = s->as<Block>();
                this->writeCommand(Rehydrator::kBlock_Command);
                AutoDehydratorSymbolTable symbols(this, b.symbolTable());
                this->writeU8(b.children().size());
                for (const std::unique_ptr<Statement>& blockStmt : b.children()) {
                    this->write(blockStmt.get());
                }
                this->writeU8(b.isScope());
                break;
            }
            case Statement::Kind::kBreak:
                this->writeCommand(Rehydrator::kBreak_Command);
                break;
            case Statement::Kind::kContinue:
                this->writeCommand(Rehydrator::kContinue_Command);
                break;
            case Statement::Kind::kDiscard:
                this->writeCommand(Rehydrator::kDiscard_Command);
                break;
            case Statement::Kind::kDo: {
                const DoStatement& d = s->as<DoStatement>();
                this->writeCommand(Rehydrator::kDo_Command);
                this->write(d.statement().get());
                this->write(d.test().get());
                break;
            }
            case Statement::Kind::kExpression: {
                const ExpressionStatement& e = s->as<ExpressionStatement>();
                this->writeCommand(Rehydrator::kExpressionStatement_Command);
                this->write(e.expression().get());
                break;
            }
            case Statement::Kind::kFor: {
                const ForStatement& f = s->as<ForStatement>();
                this->writeCommand(Rehydrator::kFor_Command);
                this->write(f.initializer().get());
                this->write(f.test().get());
                this->write(f.next().get());
                this->write(f.statement().get());
                this->write(f.symbols());
                break;
            }
            case Statement::Kind::kIf: {
                const IfStatement& i = s->as<IfStatement>();
                this->writeCommand(Rehydrator::kIf_Command);
                this->writeU8(i.isStatic());
                this->write(i.test().get());
                this->write(i.ifTrue().get());
                this->write(i.ifFalse().get());
                break;
            }
            case Statement::Kind::kInlineMarker: {
                const InlineMarker& i = s->as<InlineMarker>();
                this->writeCommand(Rehydrator::kInlineMarker_Command);
                this->writeId(i.fFuncDecl);
                break;
            }
            case Statement::Kind::kNop:
                SkDEBUGFAIL("unexpected--nop statement in finished code");
                break;
            case Statement::Kind::kReturn: {
                const ReturnStatement& r = s->as<ReturnStatement>();
                this->writeCommand(Rehydrator::kReturn_Command);
                this->write(r.fExpression.get());
                break;
            }
            case Statement::Kind::kSwitch: {
                const SwitchStatement& ss = s->as<SwitchStatement>();
                this->writeCommand(Rehydrator::kSwitch_Command);
                this->writeU8(ss.fIsStatic);
                AutoDehydratorSymbolTable symbols(this, ss.fSymbols);
                this->write(ss.fValue.get());
                this->writeU8(ss.fCases.size());
                for (const std::unique_ptr<SwitchCase>& sc : ss.fCases) {
                    this->write(sc->fValue.get());
                    this->writeU8(sc->fStatements.size());
                    for (const std::unique_ptr<Statement>& stmt : sc->fStatements) {
                        this->write(stmt.get());
                    }
                }
                break;
            }
            case Statement::Kind::kSwitchCase:
                SkDEBUGFAIL("SwitchCase statements shouldn't appear here");
                break;
            case Statement::Kind::kVarDeclaration: {
                const VarDeclaration& v = s->as<VarDeclaration>();
                this->writeCommand(Rehydrator::kVarDeclaration_Command);
                this->writeU16(this->symbolId(v.fVar));
                this->write(v.fBaseType);
                this->writeU8(v.fSizes.size());
                for (const std::unique_ptr<Expression>& sizeExpr : v.fSizes) {
                    this->write(sizeExpr.get());
                }
                this->write(v.fValue.get());
                break;
            }
            case Statement::Kind::kWhile: {
                const WhileStatement& w = s->as<WhileStatement>();
                this->writeCommand(Rehydrator::kWhile_Command);
                this->write(w.fTest.get());
                this->write(w.fStatement.get());
                break;
            }
        }
    } else {
        this->writeCommand(Rehydrator::kVoid_Command);
    }
}

void Dehydrator::write(const ProgramElement& e) {
    switch (e.kind()) {
        case ProgramElement::Kind::kEnum: {
            const Enum& en = e.as<Enum>();
            this->writeCommand(Rehydrator::kEnum_Command);
            this->write(en.typeName());
            AutoDehydratorSymbolTable symbols(this, en.symbols());
            for (const std::unique_ptr<const Symbol>& s : en.symbols()->fOwnedSymbols) {
                SkASSERT(s->kind() == Symbol::Kind::kVariable);
                Variable& v = (Variable&) *s;
                SkASSERT(v.initialValue());
                const IntLiteral& i = v.initialValue()->as<IntLiteral>();
                this->writeS32(i.value());
            }
            break;
        }
        case ProgramElement::Kind::kExtension:
            SkASSERT(false);
            break;
        case ProgramElement::Kind::kFunction: {
            const FunctionDefinition& f = e.as<FunctionDefinition>();
            this->writeCommand(Rehydrator::kFunctionDefinition_Command);
            this->writeU16(this->symbolId(&f.fDeclaration));
            this->write(f.fBody.get());
            this->writeU8(f.fReferencedIntrinsics.size());
            std::set<uint16_t> ordered;
            for (const FunctionDeclaration* ref : f.fReferencedIntrinsics) {
                ordered.insert(this->symbolId(ref));
            }
            for (uint16_t ref : ordered) {
                this->writeU16(ref);
            }
            break;
        }
        case ProgramElement::Kind::kInterfaceBlock: {
            const InterfaceBlock& i = e.as<InterfaceBlock>();
            this->writeCommand(Rehydrator::kInterfaceBlock_Command);
            this->write(*i.fVariable);
            this->write(i.fTypeName);
            this->write(i.fInstanceName);
            this->writeU8(i.fSizes.size());
            for (const auto& s : i.fSizes) {
                this->write(s.get());
            }
            break;
        }
        case ProgramElement::Kind::kModifiers:
            SkASSERT(false);
            break;
        case ProgramElement::Kind::kSection:
            SkASSERT(false);
            break;
        case ProgramElement::Kind::kGlobalVar: {
            const GlobalVarDeclaration& v = e.as<GlobalVarDeclaration>();
            this->writeCommand(Rehydrator::kVarDeclarations_Command);
            this->write(v.fDecl.get());
            break;
        }
    }
}

void Dehydrator::write(const std::vector<std::unique_ptr<ProgramElement>>& elements) {
    this->writeCommand(Rehydrator::kElements_Command);
    this->writeU8(elements.size());
    for (const auto& e : elements) {
        this->write(*e);
    }
}

void Dehydrator::finish(OutputStream& out) {
    String stringBuffer = fStringBuffer.str();
    String commandBuffer = fBody.str();

    out.write16(fStringBuffer.str().size());
    fStringBufferStart = 2;
    out.writeString(stringBuffer);
    fCommandStart = fStringBufferStart + stringBuffer.size();
    out.writeString(commandBuffer);
}

const char* Dehydrator::prefixAtOffset(size_t byte) {
    if (byte >= fCommandStart) {
        return fCommandBreaks.contains(byte - fCommandStart) ? "\n" : "";
    }
    if (byte >= fStringBufferStart) {
        return fStringBreaks.contains(byte - fStringBufferStart) ? "\n" : "";
    }
    return "";
}

} // namespace

#endif
