#include "impala/sema/unifiable.h"

#include "thorin/util/assert.h"

#include "impala/sema/type.h"
#include "impala/sema/trait.h"
#include "impala/sema/typetable.h"

namespace impala {

bool Unifiable::unify_type_vars(thorin::ArrayRef<TypeVar> other_vars) {
    if (num_type_vars() == other_vars.size())
        return !is_generic(); // TODO enable unification of generic elements!
    return false;
}

void Unifiable::refine_type_vars() {
    for (auto v : type_vars())
        v->refine();
}

bool Unifiable::type_vars_known() const {
    for (auto v : type_vars()) {
        if (!v->is_known())
            return false;
    }
    return true;
}

void Unifiable::bind(TypeVar v) {
    assert(!v->is_closed() && "type variables already bound");
    assert(!is_unified() && "type already unified");
    assert(v->bound_at_ == nullptr && "type variables can only be bound once");
    // CHECK should variables only be bound in this case? does this also hold for traits?
    //assert(v->is_subtype(this) && "Type variables can only be bound at t if they are a subtype of t!");
    // CHECK should 'forall a, a' be forbidden?
    //assert(type->kind() != Type_var && "Types like 'forall a, a' are forbidden!");

    v->bound_at_ = this;
    type_vars_.push_back(v);
}

Unifiable* Unifiable::instantiate(SpecializeMap& var_instances) {
/*#ifndef NDEBUG
    verify_instantiation(var_instances);
#endif*/
    assert(var_instances.size() == num_type_vars());
    return vspecialize(var_instances);
}

Unifiable* Unifiable::specialize(SpecializeMap& map) {
    // FEATURE this could be faster if we copy only types where something changed inside
    if (auto result = thorin::find(map, this))
        return result;

    for (auto v : type_vars()) {
        // CHECK is representative really correct or do we need node()? -- see also below!
        assert(!map.contains(v.representative()));
        v->clone(map); // CHECK is node() correct here?
    }

    Unifiable* t = vspecialize(map);

    for (auto v : type_vars()) {
        assert(map.contains(v.representative()));
        t->bind(TypeVar(map[v.representative()]->as<TypeVarNode>()));
    }

    return t;
}

bool Unifiable::unify() { return typetable().unify(this); }

}