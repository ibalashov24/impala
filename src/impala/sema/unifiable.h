#ifndef IMPALA_SEMA_UNIFIABLE_H
#define IMPALA_SEMA_UNIFIABLE_H

#include <set>
#include <stack>
#include <vector>

#include "thorin/def.h"
#include "thorin/type.h"
#include "thorin/util/array.h"
#include "thorin/util/cast.h"
#include "thorin/util/hash.h"

#include "impala/symbol.h"

namespace impala {

template<class T> using ArrayRef = thorin::ArrayRef<T>;
template<class T> using Array    = thorin::Array<T>;

class CodeGen;
class ImplItem;
class StructDecl;
class TraitDecl;
class TypeTable;
class Unifiable;

template<class T> class Proxy;
class ArrayTypeNode;            typedef Proxy<ArrayTypeNode>            ArrayType;
class BorrowedPtrTypeNode;      typedef Proxy<BorrowedPtrTypeNode>      BorrowedPtrType;
class BoundNode;                typedef Proxy<BoundNode>                Bound;
class DefiniteArrayTypeNode;    typedef Proxy<DefiniteArrayTypeNode>    DefiniteArrayType;
class FnTypeNode;               typedef Proxy<FnTypeNode>               FnType;
class ImplNode;                 typedef Proxy<ImplNode>                 Impl;
class IndefiniteArrayTypeNode;  typedef Proxy<IndefiniteArrayTypeNode>  IndefiniteArrayType;
class KnownTypeNode;            typedef Proxy<KnownTypeNode>            KnownType;
class NoRetTypeNode;            typedef Proxy<NoRetTypeNode>            NoRetType;
class OwnedPtrTypeNode;         typedef Proxy<OwnedPtrTypeNode>         OwnedPtrType;
class PrimTypeNode;             typedef Proxy<PrimTypeNode>             PrimType;
class PtrTypeNode;              typedef Proxy<PtrTypeNode>              PtrType;
class StructTypeNode;           typedef Proxy<StructTypeNode>           StructType;
class TraitNode;                typedef Proxy<TraitNode>                Trait;
class TupleTypeNode;            typedef Proxy<TupleTypeNode>            TupleType;
class TypeErrorNode;            typedef Proxy<TypeErrorNode>            TypeError;
class TypeNode;                 typedef Proxy<TypeNode>                 Type;
class TypeVarNode;              typedef Proxy<TypeVarNode>              TypeVar;
class Unifiable;                typedef Proxy<Unifiable>                Uni;
class UnknownTypeNode;          typedef Proxy<UnknownTypeNode>          UnknownType;

//------------------------------------------------------------------------------

typedef thorin::HashMap<const TypeNode*, const TypeNode*> SpecializeMap;

SpecializeMap specialize_map(const Unifiable*, ArrayRef<Type>);
/// Creates a \p SpecializeMap by mapping each of \p type's type variable to the corresponding element in \p type_args.
template<class T>
SpecializeMap specialize_map(Proxy<T> type, ArrayRef<Type> type_args) { return specialize_map(*type, type_args); }

/**
 * note: bound checking cannot be done during instantiation of the unknowns because of types like fn[A:T[B], B: T[A]](a: A, b: B)
 * therefore it is important to call \p check_bounds after all unknowns have been resolved!
 */
Type instantiate_unknown(Type, std::vector<Type>&);

//------------------------------------------------------------------------------

/**
 * Try to fill in missing type information by matching this possibly incomplete Unifiable with a complete Unifiable.
 * Example: fn(?0, ?1) unified_with fn(int, bool)  will set ?0=int and ?1=bool
 * @return \p true if unification worked, i.e. both generics were structurally equal
 *         and there were no contradictions during unification (a contradiction
 *         would be fn(?0, ?0) unified with fn(int, bool)).
 */
bool infer(Uni, Uni);

struct BoundsLT { bool operator () (Bound b1, Bound b2) const; };

template<class T>
class Proxy {
private:
    bool operator != (const Proxy<T>&) const; ///< Always test positively to allow for automagic type inference.

public:
    typedef T BaseType;

    Proxy()
        : node_(nullptr)
    {}
    Proxy(const T* node)
        : node_(node)
    {}
    Proxy(Proxy<T>&& other)
        : node_(std::move(other.node_))
    {
        other.node_ = nullptr;
    }
    Proxy(const Proxy<T>& other)
        : node_(other.node_)
    {}

    bool empty() const { return node_ == nullptr; }
    bool operator == (const Proxy<T>& other) const {
        assert(&node()->typetable() == &other.node()->typetable());
        if (this->node_ == other.node_) // TODO do we really wanna have this check?
            return true;
        return infer(*this, other) || (*this)->is_error() || other->is_error();
    }
    Proxy<T> unify() const { return node()->unify()->template as<T>(); }
    const T* representative() const { return node()->representative()->template as<T>(); }
    const T* node() const { assert(node_ != nullptr); return node_; }
    const T* operator  * () const { return node()->is_unified() ? representative() : node(); }
    const T* operator -> () const { return *(*this); }
    /// Automatic up-cast in the class hierarchy.
    template<class U> operator Proxy<U>() const {
        static_assert(std::is_base_of<U, T>::value, "U is not a base type of T");
        return Proxy<U>((**this)->template as<T>());
    }
    template<class U> Proxy<typename U::BaseType> isa() const {
        return Proxy<typename U::BaseType>((*this)->template isa<typename U::BaseType>());
    }
    template<class U> Proxy<typename U::BaseType> as() const {
        return Proxy<typename U::BaseType>((*this)->template as <typename U::BaseType>());
    }
    operator bool() { return !empty(); }
    void clear() { assert(node_ != nullptr); node_ = nullptr; }
    Proxy<T>& operator= (Proxy<T> other) { swap(*this, other); return *this; }
    friend void swap(Proxy<T>& p1, Proxy<T>& p2) {
        assert(p1.node_ == nullptr);
        auto tmp = p2.node();
        p2.node_ = p1.node_;
        p1.node_ = tmp;
    }

private:
    const T* node_;
};

//------------------------------------------------------------------------------

enum Kind {
#define IMPALA_TYPE(itype, atype) Kind_##itype,
#include "impala/tokenlist.h"
    Kind_borrowed_ptr,
    Kind_bound,
    Kind_definite_array,
    Kind_error,
    Kind_fn,
    Kind_impl,
    Kind_indefinite_array,
    Kind_noret,
    Kind_owned_ptr,
    Kind_struct,
    Kind_trait,
    Kind_tuple,
    Kind_type_var,
    Kind_unknown,
};

enum PrimTypeKind {
#define IMPALA_TYPE(itype, atype) PrimType_##itype = Kind_##itype,
#include "impala/tokenlist.h"
};

class Unifiable : public thorin::MagicCast<Unifiable> {
private:
    Unifiable& operator = (const Unifiable&); ///< Do not copy-assign a \p Unifiable.
    Unifiable(const Unifiable&);              ///< Do not copy-construct a \p Unifiable.

protected:
    Unifiable(TypeTable& tt, Kind kind, ArrayRef<Type> elems)
        : typetable_(tt)
        , kind_(kind)
        , representative_(nullptr)
        , id_(counter_++)
        , thorin_type_(nullptr)
        , elems_(elems.size())
    {
        for (size_t i = 0, e = elems.size(); i != e; ++i)
            set(i, elems[i]);
    }

    void set(size_t i, Type n) { elems_[i] = n; }
    Array<Type> specialize_elems(SpecializeMap&) const;
    void convert_elems(CodeGen& world, std::vector<thorin::Type>& nelems) const;
    std::string elems_to_string() const;

public:
    TypeTable& typetable() const { return typetable_; }
    Kind kind() const { return kind_; }
    ArrayRef<Type> elems() const { return ArrayRef<Type>(elems_); }
    const Type elem(size_t i) const { return elems_[i]; }
    size_t num_elems() const { return elems_.size(); }
    bool is_empty() const { assert(!elems_.empty() || type_vars_.empty()); return elems_.empty(); }
    ArrayRef<TypeVar> type_vars() const { return ArrayRef<TypeVar>(type_vars_); }
    TypeVar type_var(size_t i) const { return type_vars_[i]; }
    size_t num_type_vars() const { return type_vars_.size(); }
    const int id() const { return id_; }
    const Unifiable* representative() const { return representative_; }
    bool is_unified() const { return representative_ != nullptr; }
    const Unifiable* unify() const;
    void dump() const;
    /// Returns true if this \p Type does have any bound type variabes (\p type_vars_).
    bool is_polymorphic() const { return !type_vars_.empty(); }
    /**
     * A type is closed if it contains no unbound type variables.
     * \attention A closed type variable must not be changed anymore.
     */
    virtual bool is_closed() const;
    virtual void bind(TypeVar v) const;
    virtual size_t hash() const;
    virtual bool equal(const Unifiable*) const;
    virtual std::string to_string() const = 0;
    virtual bool is_error() const { return false; }
    /// A \p Unifiable is known if it does not contain any \p UnknownTypeNode%s
    virtual bool is_known() const;

protected:
    std::string type_vars_to_string() const;

private:
    virtual thorin::Type convert(CodeGen&) const = 0;

    static int counter_;

    TypeTable& typetable_;
    const Kind kind_;
    mutable const Unifiable* representative_;
    const int id_;
    mutable thorin::Type thorin_type_;
    mutable std::vector<TypeVar> type_vars_;
    std::vector<Type> elems_; ///< The operands of this type constructor.

    friend class CodeGen;
    friend class TypeTable;
    friend bool infer(const Unifiable*, const Unifiable*);
};

//------------------------------------------------------------------------------

template<class T>
std::ostream& operator << (std::ostream& o, Proxy<T> u) { return o << u->to_string(); }

//------------------------------------------------------------------------------

template<class T>
struct IdHash {
    size_t operator () (const T t) const { assert(t->is_unified() || !t->is_known()); return t->id(); }
};

template<class T>
struct IdEqual {
    bool operator () (const T t1, const T t2) const {
        assert((t1->is_unified() || !t1->is_known()) && (t1->is_unified() || !t1->is_known()));
        return t1->id() == t2->id();
    }
};

template<class T> using IdSet = thorin::HashSet<T, IdHash<T>, IdEqual<T>>;
template<class T, class U> using IdMap = thorin::HashMap<T, U, IdHash<T>, IdEqual<T>>;

//------------------------------------------------------------------------------

class TypeNode : public Unifiable {
private:
    TypeNode& operator = (const TypeNode&); ///< Do not copy-assign a \p TypeNode.
    TypeNode(const TypeNode& node);         ///< Do not copy-construct a \p TypeNode.

protected:
    TypeNode(TypeTable& typetable, Kind kind, ArrayRef<Type> elems)
        : Unifiable(typetable, kind, elems)
    {}

public:
    /// Specializes recursively this type while obeying \p map.
    Type specialize(SpecializeMap& map) const;
    /**
     * \p TypeVar%s are removed from this type.
     * They must be found in \p map in order to specialize the resulting type.
     */
    Type instantiate(SpecializeMap& map) const;
    Type instantiate(ArrayRef<Type>) const;

    virtual bool implements(Bound, SpecializeMap&) const = 0;
    /// @return The method type or an empty type if no method with this name was found
    virtual FnType find_method(Symbol s) const = 0;
    bool is_noret() const { return isa<NoRetTypeNode>(); }
    bool is(PrimTypeKind kind) const;
#define IMPALA_TYPE(itype, atype) bool is_##itype() const { return is(PrimType_##itype); }
#include "impala/tokenlist.h"

    /**
     * A type is sane if all type variables are bound correctly,
     * i.e. forall type variables v, v is a subtype of v.bound_at(). -- TODO WHAT?
     *
     * This also means that a sane type is always closed!
     */
    virtual bool is_sane() const = 0;

private:
    virtual Type vinstantiate(SpecializeMap&) const = 0;
};

class UnknownTypeNode : public TypeNode {
private:
    UnknownTypeNode(TypeTable& typetable)
        : TypeNode(typetable, Kind_unknown, {})
    {}

public:
    virtual bool is_known() const override { return false; }
    virtual size_t hash() const override { THORIN_UNREACHABLE; }
    virtual bool equal(const Unifiable*) const override { THORIN_UNREACHABLE; }
    virtual bool implements(Bound bound, SpecializeMap& map) const override { THORIN_UNREACHABLE; }
    virtual FnType find_method(Symbol s) const override { THORIN_UNREACHABLE; }
    virtual bool is_sane() const override { THORIN_UNREACHABLE; }
    virtual std::string to_string() const override;

private:
    virtual Type vinstantiate(SpecializeMap&) const override;
    virtual thorin::Type convert(CodeGen&) const override { THORIN_UNREACHABLE; }

    friend class TypeTable;
};

class KnownTypeNode : public TypeNode {
protected:
    KnownTypeNode(TypeTable& typetable, Kind kind, ArrayRef<Type> elems)
        : TypeNode(typetable, kind, elems)
    {}

public:
    const std::vector<Impl>& impls() const { return impls_; }
    void add_impl(Impl) const;

    Impl find_impl(Bound) const;
    virtual bool implements(Bound, SpecializeMap&) const;
    virtual FnType find_method(Symbol s) const;
    virtual bool is_sane() const;

private:
    mutable std::vector<Impl> impls_;

    friend class TypeTable;
};

class TypeErrorNode : public KnownTypeNode {
private:
    TypeErrorNode(TypeTable& typetable)
        : KnownTypeNode(typetable, Kind_error, {})
    {}

public:
    virtual bool is_error() const override { return true; }
    virtual std::string to_string() const { return "<type error>"; }

private:
    virtual Type vinstantiate(SpecializeMap&) const;
    virtual thorin::Type convert(CodeGen&) const { assert(false); return thorin::Type(); }

    friend class TypeTable;
};

class NoRetTypeNode : public KnownTypeNode {
private:
    NoRetTypeNode(TypeTable& typetable)
        : KnownTypeNode(typetable, Kind_noret, {})
    {}

public:
    virtual std::string to_string() const override { return "<no-return>"; }

private:
    virtual Type vinstantiate(SpecializeMap&) const;
    virtual thorin::Type convert(CodeGen&) const override;

    friend class TypeTable;
};

class PrimTypeNode : public KnownTypeNode {
private:
    PrimTypeNode(TypeTable& typetable, PrimTypeKind kind)
        : KnownTypeNode(typetable, (Kind) kind, {})
    {}

public:
    PrimTypeKind primtype_kind() const { return (PrimTypeKind) kind(); }
    virtual std::string to_string() const;

private:
    virtual Type vinstantiate(SpecializeMap&) const;
    virtual thorin::Type convert(CodeGen&) const;

    friend class TypeTable;
};

class FnTypeNode : public KnownTypeNode {
private:
    FnTypeNode(TypeTable& typetable, ArrayRef<Type> elems)
        : KnownTypeNode(typetable, Kind_fn, elems)
    {}

public:
    Type return_type() const;
    virtual std::string to_string() const { return std::string("fn") + type_vars_to_string() + elems_to_string(); }

private:
    virtual Type vinstantiate(SpecializeMap&) const;
    virtual thorin::Type convert(CodeGen&) const;

    friend class TypeTable;
};

class TupleTypeNode : public KnownTypeNode {
private:
    TupleTypeNode(TypeTable& typetable, ArrayRef<Type> elems)
        : KnownTypeNode(typetable, Kind_tuple, elems)
    {}

public:
    virtual std::string to_string() const { return type_vars_to_string() + elems_to_string(); }

private:
    virtual Type vinstantiate(SpecializeMap&) const;
    virtual thorin::Type convert(CodeGen&) const;

    friend class TypeTable;
};

class StructTypeNode : public KnownTypeNode {
private:
    StructTypeNode(TypeTable& typetable, const StructDecl* struct_decl);

public:
    const StructDecl* struct_decl() const { return struct_decl_; }
    virtual std::string to_string() const { return "TODO"; }

private:
    virtual Type vinstantiate(SpecializeMap&) const;
    virtual thorin::Type convert(CodeGen&) const;

    const StructDecl* struct_decl_;

    friend class TypeTable;
};

class TypeVarNode : public KnownTypeNode {
private:
    TypeVarNode(TypeTable& tt, Symbol name)
        : KnownTypeNode(tt, Kind_type_var, {})
        , name_(name)
        , bound_at_(nullptr)
        , equiv_(nullptr)
    {}

    bool bounds_equal(const TypeVarNode*) const;

public:
    const std::vector<Bound>& bounds() const { return bounds_; }
    Bound bound(size_t i) const { return bounds_[i]; }
    size_t num_bounds() const { return bounds_.size(); }
    const Unifiable* bound_at() const { return bound_at_; }
    void add_bound(Bound) const;

    virtual bool is_closed() const { return bound_at_ != nullptr; }
    virtual bool is_sane() const { return is_closed(); }
    virtual bool equal(const Unifiable*) const;
    virtual bool implements(Bound, SpecializeMap&) const;
    virtual FnType find_method(Symbol s) const;
    virtual std::string to_string() const;

private:
    virtual Type vinstantiate(SpecializeMap&) const;
    virtual thorin::Type convert(CodeGen&) const { assert(false); return thorin::Type(); }

    Symbol name_;
    mutable std::vector<Bound> bounds_; ///< All traits that restrict the instantiation of this variable.
    mutable const Unifiable* bound_at_; ///< The type where this variable is bound.
    mutable const TypeVarNode* equiv_;  ///< Used to define equivalence constraints when checking equality of types.

public: // TODO make private
    mutable std::stack<thorin::Def> defs_;

    friend class TypeTable;
    friend void Unifiable::bind(TypeVar) const;
    friend bool Unifiable::equal(const Unifiable*) const;
};

//------------------------------------------------------------------------------

class PtrTypeNode : public KnownTypeNode {
public:
    PtrTypeNode(TypeTable& typetable, Kind kind, Type referenced_type)
        : KnownTypeNode(typetable, kind, { referenced_type })
    {}

    Type referenced_type() const { return elem(0); }

private:
    virtual thorin::Type convert(CodeGen&) const override;
};

class OwnedPtrTypeNode : public PtrTypeNode {
public:
    OwnedPtrTypeNode(TypeTable& typetable, Type referenced_type)
        : PtrTypeNode(typetable, Kind_owned_ptr, referenced_type)
    {}

    virtual std::string to_string() const override;

private:
    virtual Type vinstantiate(SpecializeMap&) const override;
};

class BorrowedPtrTypeNode : public PtrTypeNode {
public:
    BorrowedPtrTypeNode(TypeTable& typetable, Type referenced_type)
        : PtrTypeNode(typetable, Kind_borrowed_ptr, referenced_type)
    {}

    virtual std::string to_string() const override;

private:
    virtual Type vinstantiate(SpecializeMap&) const override;
};

//------------------------------------------------------------------------------

class ArrayTypeNode : public KnownTypeNode {
public:
    ArrayTypeNode(TypeTable& typetable, Kind kind, Type elem_type)
        : KnownTypeNode(typetable, kind, { elem_type })
    {}

    Type elem_type() const { return elem(0); }
};

class DefiniteArrayTypeNode : public ArrayTypeNode {
public:
    DefiniteArrayTypeNode(TypeTable& typetable, Type elem_type, uint64_t dim)
        : ArrayTypeNode(typetable, Kind_definite_array, elem_type)
        , dim_(dim)
    {}

    uint64_t dim() const { return dim_; }
    virtual std::string to_string() const override;

private:
    virtual Type vinstantiate(SpecializeMap&) const override;
    virtual thorin::Type convert(CodeGen&) const override;

    const uint64_t dim_;
};

class IndefiniteArrayTypeNode : public ArrayTypeNode {
public:
    IndefiniteArrayTypeNode(TypeTable& typetable, Type elem_type)
        : ArrayTypeNode(typetable, Kind_indefinite_array, elem_type)
    {}

    virtual std::string to_string() const override;

private:
    virtual Type vinstantiate(SpecializeMap&) const override;
    virtual thorin::Type convert(CodeGen&) const override;
};

//------------------------------------------------------------------------------

/**
 * Represents a declared trait.
 * A trait consists of a name, a number of declared methods and a number of
 * super traits. Also, it may be generic in a number of type variables that
 * can be restricted by any number of instantiated traits.
 *
 * The restrictions for the traits must not include the newly declared trait
 * itself. Otherwise things get complicated, e.g. the following would be
 * allowed (I guess):
 * @code trait TT<X:TT<Self>> {}; impl TT<int> for int {} @endcode
 *
 * @see BoundNode
 */
class TraitNode : public Unifiable {
private:
    TraitNode(TypeTable& tt, const TraitDecl* trait_decl)
        : Unifiable(tt, Kind_trait, {})
        , trait_decl_(trait_decl)
    {}

public:
    typedef std::set<Bound, BoundsLT> SuperBounds;

    const TraitDecl* trait_decl() const { return trait_decl_; }
    const SuperBounds& super_bounds() const { return super_bounds_; }
    Bound super_bound(Trait trait) const;
    const std::vector<Impl>& type2impls(Type type) const { return type2impls_[type]; }
    bool add_super_bound(Bound) const;
    /// return the type of the method with this name if it exists; otherwise return an empty type
    FnType find_method(Symbol name) const;
    bool has_method(Symbol name) const { return !find_method(name).empty(); }
    Bound instantiate(ArrayRef<Type> args) const;
    void add_impl(Impl impl) const;

    virtual bool is_error() const override { return trait_decl() == nullptr; }
    virtual size_t hash() const override;
    virtual bool equal(const Unifiable*) const override;
    virtual std::string to_string() const;

private:
    virtual thorin::Type convert(CodeGen&) const override;

    const TraitDecl* const trait_decl_;
    mutable SuperBounds super_bounds_;
    mutable IdMap<Type, std::vector<Impl>> type2impls_;

    friend class TypeTable;
};

//------------------------------------------------------------------------------

/// An instance of a trait is a trait where all type variables are instantiated by concrete types.
class BoundNode : public Unifiable {
private:
    BoundNode(const Trait trait, ArrayRef<Type> elems)
        : Unifiable(trait->typetable(), Kind_bound, elems)
        , trait_(trait.unify())
    {
        assert(trait_->num_type_vars() == num_elems());
    }

public:
    const Trait trait() const { return trait_; }
    FnType find_method(Symbol name) const;
    Bound specialize(SpecializeMap&) const;

    virtual bool is_error() const override { return trait()->is_error(); }
    virtual size_t hash() const override;
    virtual bool equal(const Unifiable*) const override;
    virtual std::string to_string() const;

private:
    virtual thorin::Type convert(CodeGen&) const override;

    const Trait trait_;
    mutable thorin::HashMap<Symbol, FnType> method_cache_;

    friend class TypeTable;
};

//------------------------------------------------------------------------------

class ImplNode : public Unifiable {
private:
    ImplNode(TypeTable& tt, const ImplItem* impl_item, Bound bound, Type type)
        : Unifiable(tt, Kind_impl, {type})
        , impl_item_(impl_item)
        , bound_(bound)
    {}

public:
    const ImplItem* impl_item() const { return impl_item_; }
    Bound bound() const { return bound_; }
    Type type() const { return elem(0); }
    Impl specialize(SpecializeMap& map) const;

    virtual size_t hash() const;
    virtual bool equal(const Unifiable*) const { THORIN_UNREACHABLE; }
    virtual std::string to_string() const { return ""; } // TODO

private:
    virtual thorin::Type convert(CodeGen&) const override;

    const ImplItem* const impl_item_;
    Bound bound_;
    Type type_;

    friend class TypeTable;
};

//------------------------------------------------------------------------------

}

#endif
