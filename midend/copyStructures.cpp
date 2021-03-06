/*
Copyright 2016 VMware, Inc.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
*/

#include "copyStructures.h"

namespace P4 {

const IR::Node* DoCopyStructures::postorder(IR::AssignmentStatement* statement) {
    auto ltype = typeMap->getType(statement->left, true);
    if (ltype->is<IR::Type_StructLike>()) {
        auto retval = new IR::Vector<IR::StatOrDecl>();
        auto strct = ltype->to<IR::Type_StructLike>();
        if (statement->right->is<IR::ListExpression>()) {
            auto list = statement->right->to<IR::ListExpression>();
            unsigned index = 0;
            for (auto f : *strct->fields) {
                auto right = list->components->at(index);
                auto left = new IR::Member(Util::SourceInfo(), statement->left, f->name);
                retval->push_back(new IR::AssignmentStatement(statement->srcInfo, left, right));
                index++;
            }
        } else {
            if (ltype->is<IR::Type_Header>())
                // Leave headers as they are -- copy_header will also copy the valid bit
                return statement;

            for (auto f : *strct->fields) {
                auto right = new IR::Member(Util::SourceInfo(), statement->right, f->name);
                auto left = new IR::Member(Util::SourceInfo(), statement->left, f->name);
                retval->push_back(new IR::AssignmentStatement(statement->srcInfo, left, right));
            }
        }
        return retval;
    } else if (ltype->is<IR::Type_Stack>()) {
        auto retval = new IR::Vector<IR::StatOrDecl>();
        auto stack = ltype->to<IR::Type_Stack>();
        for (unsigned i = 0; i < stack->getSize(); i++) {
            auto index = new IR::Constant(i);
            auto right = new IR::ArrayIndex(Util::SourceInfo(), statement->right, index);
            auto left = new IR::ArrayIndex(Util::SourceInfo(), statement->left, index->clone());
            retval->push_back(new IR::AssignmentStatement(statement->srcInfo, left, right));
        }
        return retval;
    }

    return statement;
}

}  // namespace P4
