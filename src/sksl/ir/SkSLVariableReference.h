/*
 * Copyright 2016 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef SKSL_VARIABLEREFERENCE
#define SKSL_VARIABLEREFERENCE

#include "src/sksl/ir/SkSLExpression.h"

namespace SkSL {

class IRGenerator;
class Variable;

/**
 * A reference to a variable, through which it can be read or written. In the statement:
 *
 * x = x + 1;
 *
 * there is only one Variable 'x', but two VariableReferences to it.
 */
class VariableReference : public Expression {
public:
    static constexpr Kind kExpressionKind = Kind::kVariableReference;

    enum RefKind {
        kRead_RefKind,
        kWrite_RefKind,
        kReadWrite_RefKind,
        // taking the address of a variable - we consider this a read & write but don't complain if
        // the variable was not previously assigned
        kPointer_RefKind
    };

    VariableReference(int offset, const Variable* variable, RefKind refKind = kRead_RefKind);

    ~VariableReference() override;

    VariableReference(const VariableReference&) = delete;
    VariableReference& operator=(const VariableReference&) = delete;

    const Type& type() const override;

    const Variable* variable() const {
        return this->variableReferenceData().fVariable;
    }

    RefKind refKind() const {
        return (RefKind) this->variableReferenceData().fRefKind;
    }

    void setRefKind(RefKind refKind);
    void setVariable(const Variable* variable);

    bool hasProperty(Property property) const override;

    bool isConstantOrUniform() const override;

    std::unique_ptr<Expression> clone() const override {
        return std::unique_ptr<Expression>(new VariableReference(fOffset, this->variable(),
                                                                 this->refKind()));
    }

    String description() const override;

    std::unique_ptr<Expression> constantPropagate(const IRGenerator& irGenerator,
                                                  const DefinitionMap& definitions) override;

private:
    using INHERITED = Expression;
};

}  // namespace SkSL

#endif
