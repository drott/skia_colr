/*
 * Copyright 2020 Google LLC.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "src/sksl/ir/SkSLIRNode.h"

#include "src/sksl/ir/SkSLExpression.h"

namespace SkSL {

IRNode::IRNode(int offset, int kind, const BlockData& data,
               std::vector<std::unique_ptr<Statement>> stmts)
: fOffset(offset)
, fKind(kind)
, fData(data)
, fStatementChildren(std::move(stmts)) {}

IRNode::IRNode(int offset, int kind, const BoolLiteralData& data)
: fOffset(offset)
, fKind(kind)
, fData(data) {}

IRNode::IRNode(int offset, int kind, const EnumData& data)
: fOffset(offset)
, fKind(kind)
, fData(data) {}

IRNode::IRNode(int offset, int kind, const ExternalValueData& data)
: fOffset(offset)
, fKind(kind)
, fData(data) {}

IRNode::IRNode(int offset, int kind, const FieldData& data)
: fOffset(offset)
, fKind(kind)
, fData(data) {}

IRNode::IRNode(int offset, int kind, const FloatLiteralData& data)
: fOffset(offset)
, fKind(kind)
, fData(data) {}

IRNode::IRNode(int offset, int kind, const FunctionCallData& data)
: fOffset(offset)
, fKind(kind)
, fData(data) {}

IRNode::IRNode(int offset, int kind, const FunctionDeclarationData& data)
: fOffset(offset)
, fKind(kind)
, fData(data) {}

IRNode::IRNode(int offset, int kind, const IfStatementData& data)
: fOffset(offset)
, fKind(kind)
, fData(data) {}

IRNode::IRNode(int offset, int kind, const IntLiteralData& data)
: fOffset(offset)
, fKind(kind)
, fData(data) {}

IRNode::IRNode(int offset, int kind, const ForStatementData& data)
: fOffset(offset)
, fKind(kind)
, fData(data) {}

IRNode::IRNode(int offset, int kind, const SettingData& data)
: fOffset(offset)
, fKind(kind)
, fData(data) {}

IRNode::IRNode(int offset, int kind, const String& data)
: fOffset(offset)
, fKind(kind)
, fData(data) {}

IRNode::IRNode(int offset, int kind, const SymbolData& data)
: fOffset(offset)
, fKind(kind)
, fData(data) {}

IRNode::IRNode(int offset, int kind, const SymbolAliasData& data)
: fOffset(offset)
, fKind(kind)
, fData(data) {}

IRNode::IRNode(int offset, int kind, const Type* data)
: fOffset(offset)
, fKind(kind)
, fData(data) {}

IRNode::IRNode(int offset, int kind, const TypeTokenData& data)
: fOffset(offset)
, fKind(kind)
, fData(data) {}

IRNode::IRNode(int offset, int kind, const VariableData& data)
: fOffset(offset)
, fKind(kind)
, fData(data) {}

IRNode::IRNode(int offset, int kind, const VariableReferenceData& data)
: fOffset(offset)
, fKind(kind)
, fData(data) {}

IRNode::~IRNode() {}

} // namespace SkSL
