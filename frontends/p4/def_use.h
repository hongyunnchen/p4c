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

#ifndef _FRONTENDS_P4_DEF_USE_H_
#define _FRONTENDS_P4_DEF_USE_H_

#include "ir/ir.h"
#include "frontends/p4/typeChecking/typeChecker.h"

namespace P4 {

class StorageFactory;
class LocationSet;

// Abstraction for something that is has a left value (variable, parameter)
class StorageLocation : public IHasDbPrint {
 public:
    virtual ~StorageLocation() {}
    const IR::Type* type;
    const cstring name;
    StorageLocation(const IR::Type* type, cstring name) : type(type), name(name)
    { CHECK_NULL(type); }
    template <class T>
    const T* to() const {
        auto result = dynamic_cast<const T*>(this);
        CHECK_NULL(result);
        return result;
    }
    template <class T>
    bool is() const {
        auto result = dynamic_cast<const T*>(this);
        return result != nullptr;
    }
    virtual void dbprint(std::ostream& out) const {
        out << name;
    }
    cstring toString() const { return name; }

    // all locations inside that represent valid bits
    const LocationSet* getValidBits() const;
    virtual void addValidBits(LocationSet* result) const = 0;
    // set of locations if we exclude all headers
    const LocationSet* removeHeaders() const;
    virtual void removeHeaders(LocationSet* result) const = 0;
};

/* Represents a storage location with a simple type.
   It could be either a scalar variable, or a field of a struct, etc. */
class BaseLocation : public StorageLocation {
 public:
    BaseLocation(const IR::Type* type, cstring name) :
            StorageLocation(type, name)
    { BUG_CHECK(type->is<IR::Type_Base>() || type->is<IR::Type_Enum>()
                || type->is<IR::Type_Var>() || type->is<IR::Type_Tuple>()
                || type-is<IR::Type_Error>() || type->is<IR::Type_Var>(),
                "%1%: unexpected type", type); }
    void addValidBits(LocationSet*) const override {}
    void removeHeaders(LocationSet* result) const override;
};

class StructLocation : public StorageLocation {
    std::map<cstring, const StorageLocation*> fieldLocations;
    friend class StorageFactory;

    void addField(cstring name, StorageLocation* field)
    { fieldLocations.emplace(name, field); CHECK_NULL(field); }
 public:
    StructLocation(const IR::Type* type, cstring name) :
            StorageLocation(type, name) {
        BUG_CHECK(type->is<IR::Type_StructLike>(),
                  "%1%: unexpected type", type);
    }
    IterValues<std::map<cstring, const StorageLocation*>::const_iterator> fields() const
    { return Values(fieldLocations); }
    void dbprint(std::ostream& out) const override {
        for (auto f : fieldLocations)
            out << *f.second << " ";
    }
    void addField(cstring field, LocationSet* addTo) const;
    void addValidBits(LocationSet* result) const override;
    void removeHeaders(LocationSet* result) const override;
};

class ArrayLocation : public StorageLocation {
    std::vector<const StorageLocation*> elements;
    friend class StorageFactory;

    void addElement(unsigned index, StorageLocation* element)
    { elements[index] = element; CHECK_NULL(element); }

 public:
    ArrayLocation(const IR::Type* type, cstring name) :
            StorageLocation(type, name) {
        BUG_CHECK(type->is<IR::Type_Stack>(), "%1%: unexpected type", type);
        auto stack = type->to<IR::Type_Stack>();
        elements.resize(stack->getSize());
    }
    std::vector<const StorageLocation*>::const_iterator begin() const
    { return elements.cbegin(); }
    std::vector<const StorageLocation*>::const_iterator end() const
    { return elements.cend(); }
    void dbprint(std::ostream& out) const override {
        for (unsigned i = 0; i < elements.size(); i++)
            out << *elements.at(i) << " ";
    }
    void addElement(unsigned index, LocationSet* result) const;
    void addValidBits(LocationSet* result) const override;
    void removeHeaders(LocationSet*) const override {}  // no results added
};

class StorageFactory {
    TypeMap* typeMap;
 public:
    explicit StorageFactory(TypeMap* typeMap) : typeMap(typeMap)
    { CHECK_NULL(typeMap); }
    StorageLocation* create(const IR::Type* type, cstring name) const;

    static const cstring validFieldName;
};

// A set of locations that may be read or written by a computation.
// In general this is a conservative approximation of the actual location set.
class LocationSet : public IHasDbPrint {
    std::set<const StorageLocation*> locations;

 public:
    LocationSet() = default;
    explicit LocationSet(const std::set<const StorageLocation*> &other) : locations(other) {}
    explicit LocationSet(const StorageLocation* location) { locations.emplace(location); }
    static const LocationSet* empty;

    const LocationSet* getField(cstring field) const;
    const LocationSet* getValidField() const;
    const LocationSet* getIndex(unsigned index) const;
    const LocationSet* allElements() const;
    void add(const StorageLocation* location)
    { locations.emplace(location); }
    const LocationSet* join(const LocationSet* other) const;
    // express this location set only in terms of BaseLocation;
    // e.g., a StructLocation is expanded in all its fields.
    const LocationSet* canonicalize() const;
    void addCanonical(const StorageLocation* location);
    std::set<const StorageLocation*>::const_iterator begin() const { return locations.cbegin(); }
    std::set<const StorageLocation*>::const_iterator end()   const { return locations.cend(); }
    virtual void dbprint(std::ostream& out) const {
        if (locations.empty())
            out << "LocationSet::empty";
        for (auto l : locations) {
            l->dbprint(out);
            out << " ";
        }
    }
    // only defined for canonical representations
    bool overlaps(const LocationSet* other) const;
    bool isEmpty() const { return locations.empty(); }
};

// For each declaration we keep the associated storage
class StorageMap {
    std::map<const IR::IDeclaration*, StorageLocation*> storage;
    StorageFactory factory;
 public:
    ReferenceMap*  refMap;
    TypeMap*       typeMap;

    StorageMap(ReferenceMap* refMap, TypeMap* typeMap) :
            factory(typeMap), refMap(refMap), typeMap(typeMap)
    { CHECK_NULL(refMap); CHECK_NULL(typeMap); }
    StorageLocation* add(const IR::IDeclaration* decl) {
        CHECK_NULL(decl);
        auto type = typeMap->getType(decl->getNode(), true);
        auto loc = factory.create(type, decl->getName());
        if (loc != nullptr)
            storage.emplace(decl, loc);
        return loc;
    }
    StorageLocation* getOrAdd(const IR::IDeclaration* decl) {
        auto s = getStorage(decl);
        if (s != nullptr)
            return s;
        return add(decl);
    }
    StorageLocation* getStorage(const IR::IDeclaration* decl) const {
        CHECK_NULL(decl);
        auto result = ::get(storage, decl);
        return result;
    }
};

// Indicates a statement in the program.
// The stack is for representing calls: i.e.,
// table.apply() -> table -> action
class ProgramPoint : public IHasDbPrint {
    // empty stack represents "beforeStart" (see below)
    std::vector<const IR::Node*> stack;

 public:
    ProgramPoint() = default;
    ProgramPoint(const ProgramPoint& other) : stack(other.stack) {}
    explicit ProgramPoint(const IR::Node* node) { stack.push_back(node); }
    ProgramPoint(const ProgramPoint& context, const IR::Node* node);
    static ProgramPoint beforeStart;  // a point logically before the program start
    bool operator==(const ProgramPoint& other) const;
    std::size_t hash() const;
    void dbprint(std::ostream& out) const {
        if (isBeforeStart()) {
            out << "<BeforeStart>";
        } else {
            bool first = true;
            for (auto n : stack) {
                if (!first)
                    out << "//";
                out << dbp(n);
                first = false;
            }
            auto l = stack.back();
            if (l->is<IR::AssignmentStatement>() ||
                l->is<IR::MethodCallStatement>())
                out << "[[" << l << "]]";
        }
    }
    const IR::Node* last() const
    { return stack.empty() ? nullptr : stack.back(); }
    bool isBeforeStart() const
    { return stack.empty(); }
};
}  // namespace P4

#if 0
// hash and comparator for ProgramPoint
inline bool operator==(const P4::ProgramPoint& left, const P4::ProgramPoint& right) {
    return left.operator==(right);
}
#endif

// inject hash into std namespace so it is picked up by std::unordered_set
namespace std {
template<> struct hash<P4::ProgramPoint> {
    typedef P4::ProgramPoint argument_type;
    typedef std::size_t  result_type;
    result_type operator()(argument_type const &s) const {
        return s.hash();
    }
};
}  // namespace std

namespace P4 {
class ProgramPoints : public IHasDbPrint {
    typedef std::unordered_set<ProgramPoint> Points;
    Points points;
    explicit ProgramPoints(const Points &points) : points(points) {}
 public:
    ProgramPoints() = default;
    explicit ProgramPoints(ProgramPoint point) { points.emplace(point); }
    void add(ProgramPoint point) { points.emplace(point); }
    const ProgramPoints* merge(const ProgramPoints* with) const;
    bool operator==(const ProgramPoints& other) const;
    void dbprint(std::ostream& out) const {
        out << "{";
        for (auto p : points)
            out << p << " ";
        out << "}";
    }
    size_t size() const { return points.size(); }
    bool containsBeforeStart() const
    { return points.find(ProgramPoint::beforeStart) != points.end(); }
    Points::const_iterator begin() const
    { return points.cbegin(); }
    Points::const_iterator end() const
    { return points.cend(); }
};

// List of definers for each base storage (at a specific program point)
class Definitions : public IHasDbPrint {
    // which program points have written last to each location
    // (conservative approximation)
    std::map<const BaseLocation*, const ProgramPoints*> definitions;

 public:
    Definitions() = default;
    Definitions(const Definitions& other) : definitions(other.definitions) {}
    Definitions* join(const Definitions* other) const;
    // point writes the specified LocationSet
    Definitions* writes(ProgramPoint point, const LocationSet* locations) const;
    void set(const BaseLocation* loc, const ProgramPoints* point)
    { CHECK_NULL(loc); CHECK_NULL(point); definitions[loc] = point; }
    void set(const StorageLocation* loc, const ProgramPoints* point);
    void set(const LocationSet* loc, const ProgramPoints* point);
    const ProgramPoints* get(const BaseLocation* location) const {
        auto r = ::get(definitions, location);
        BUG_CHECK(r != nullptr, "%1%: no definitions", location);
        return r; }
    const ProgramPoints* get(const LocationSet* locations) const;
    bool operator==(const Definitions& other) const;
    void dbprint(std::ostream& out) const {
        if (definitions.empty())
            out << "  Empty definitions";
        bool first = true;
        for (auto d : definitions) {
            if (!first)
                out << std::endl;
            out << "  " << *d.first << "=>" << *d.second;
            first = false;
        }
    }
    Definitions* clone() const { return new Definitions(*this); }
    void remove(const StorageLocation* loc);
    bool empty() const { return definitions.empty(); }
};

class AllDefinitions : public IHasDbPrint {
    // These are the definitions available AFTER each ProgramPoint.
    // However, for ProgramPoints representing P4Control, P4Action, and P4Table
    // the definitions are BEFORE the ProgramPoint.
    std::unordered_map<ProgramPoint, Definitions*> atPoint;
 public:
    StorageMap* storageMap;
    AllDefinitions(ReferenceMap* refMap, TypeMap* typeMap) :
            storageMap(new StorageMap(refMap, typeMap)) {}
    Definitions* get(ProgramPoint point, bool emptyIfNotFound = false) {
        auto it = atPoint.find(point);
        if (it == atPoint.end()) {
            if (emptyIfNotFound) {
                auto defs = new Definitions();
                set(point, defs);
                return defs;
            }
            BUG("Unknown point %1% for definitions", &point);
        }
        return it->second;
    }
    void set(ProgramPoint point, Definitions* defs)
    { atPoint[point] = defs; }
    void dbprint(std::ostream& out) const {
        for (auto e : atPoint)
            out << e.first << " => " << e.second << std::endl;
    }
};

// This does not scan variable initializers, so it must be executed
// after these have been removed.
// Run for each parser and control separately.
// Computes the write set for each expression and statement.
// We are controlling precisely the visit order - to simulate a
// simbolic execution of the program.
class ComputeWriteSet : public Inspector {
 protected:
    AllDefinitions*     definitions;  // result
    Definitions*        currentDefinitions;  // before statement currently processed
    Definitions*        returnedDefinitions;  // set by return statements
    Definitions*        exitDefinitions;  // set by exit statements
    ProgramPoint        callingContext;
    const StorageMap*   storageMap;
    bool                lhs;    // if true we are processing an
                                // expression on the lhs of an
                                // assignment
    // For each expression the location set it writes
    std::map<const IR::Expression*, const LocationSet*> writes;

    // Create new visitor, but with same underlying data structures.
    // Needed to visit some program fragments repeatedly.
    ComputeWriteSet(const ComputeWriteSet* source, ProgramPoint context, Definitions* definitions) :
            definitions(source->definitions), currentDefinitions(definitions),
            returnedDefinitions(nullptr), exitDefinitions(source->exitDefinitions),
            callingContext(context), storageMap(source->storageMap), lhs(false) {
        setName("ComputeWriteSet");
    }
    void enterScope(const IR::ParameterList* parameters,
                    const IR::IndexedVector<IR::Declaration>* locals,
                    ProgramPoint startPoint, bool clear = true);
    void exitScope(const IR::ParameterList* parameters,
                   const IR::IndexedVector<IR::Declaration>* locals);
    Definitions* getDefinitionsAfter(const IR::ParserState* state);
    bool setDefinitions(Definitions* defs, const IR::Node* who = nullptr);
    ProgramPoint getProgramPoint(const IR::Node* node = nullptr) const;
    const LocationSet* get(const IR::Expression* expression) const {
        auto result = ::get(writes, expression);
        BUG_CHECK(result != nullptr, "No location set known for %1%", expression);
        return result;
    }
    void set(const IR::Expression* expression, const LocationSet* loc) {
        CHECK_NULL(expression); CHECK_NULL(loc);
        writes.emplace(expression, loc);
    }

 public:
    explicit ComputeWriteSet(AllDefinitions* definitions) :
            definitions(definitions), currentDefinitions(nullptr),
            returnedDefinitions(nullptr), exitDefinitions(nullptr),
            storageMap(definitions->storageMap), lhs(false)
    { CHECK_NULL(definitions); setName("ComputeWriteSet"); }

    // expressions
    bool preorder(const IR::Literal* expression) override;
    bool preorder(const IR::Slice* expression) override;
    bool preorder(const IR::TypeNameExpression* expression) override;
    bool preorder(const IR::PathExpression* expression) override;
    bool preorder(const IR::Member* expression) override;
    bool preorder(const IR::ArrayIndex* expression) override;
    bool preorder(const IR::Operation_Binary* expression) override;
    bool preorder(const IR::Mux* expression) override;
    bool preorder(const IR::SelectExpression* expression) override;
    bool preorder(const IR::ListExpression* expression) override;
    bool preorder(const IR::Operation_Unary* expression) override;
    bool preorder(const IR::MethodCallExpression* expression) override;
    bool preorder(const IR::DefaultExpression* expression) override;
    // statements
    bool preorder(const IR::ParserState* state) override;
    bool preorder(const IR::P4Parser* parser) override;
    bool preorder(const IR::P4Control* control) override;
    bool preorder(const IR::P4Action* action) override;
    bool preorder(const IR::P4Table* table) override;
    bool preorder(const IR::Function* function) override;
    bool preorder(const IR::AssignmentStatement* statement) override;
    bool preorder(const IR::ReturnStatement* statement) override;
    bool preorder(const IR::ExitStatement* statement) override;
    bool preorder(const IR::IfStatement* statement) override;
    bool preorder(const IR::BlockStatement* statement) override;
    bool preorder(const IR::SwitchStatement* statement) override;
    bool preorder(const IR::EmptyStatement* statement) override;
    bool preorder(const IR::MethodCallStatement* statement) override;

    const LocationSet* writtenLocations(const IR::Expression* expression) {
        expression->apply(*this);
        return get(expression);
    }
};

}  // namespace P4

#endif /* _FRONTENDS_P4_DEF_USE_H_ */
