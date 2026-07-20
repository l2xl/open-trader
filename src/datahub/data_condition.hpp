// Open Trader
// Copyright (c) 2026 l2xl (l2xl/at/proton.me)
// Distributed under the Intellectual Property Reserve License, v2 (IPRL)

#ifndef DATAHUB_DATA_CONDITION_HPP
#define DATAHUB_DATA_CONDITION_HPP

#include <functional>
#include <vector>
#include <string>
#include <type_traits>

#include "metadata.hpp"
#include "query_builder.hpp"

namespace datahub {

// Compile-time field predicate: field pointer + comparison operator known at compile time, value at runtime
template<typename Entity, auto Field, QueryOperator Op>
struct field_predicate
{
    using field_type = std::decay_t<decltype(std::declval<Entity>().*Field)>;

    static constexpr std::size_t field_index = detail::find_member_index<Entity, Field>();
    static constexpr std::string_view field_name = glz::member_names<Entity>[field_index];

    field_type value;

    explicit field_predicate(field_type val) : value(std::move(val)) {}

    bool matches(const Entity& e) const
    {
        const auto& field_val = e.*Field;
        if constexpr (Op == QueryOperator::Equal)              return field_val == value;
        else if constexpr (Op == QueryOperator::NotEqual)      return field_val != value;
        else if constexpr (Op == QueryOperator::LessThan)      return field_val < value;
        else if constexpr (Op == QueryOperator::LessThanOrEqual) return field_val <= value;
        else if constexpr (Op == QueryOperator::GreaterThan)   return field_val > value;
        else if constexpr (Op == QueryOperator::GreaterThanOrEqual) return field_val >= value;
        else static_assert(sizeof(Entity) == 0, "Unsupported operator for field_predicate");
    }

    QueryCondition to_query_condition() const
    {
        return QueryCondition::where(std::string(field_name), Op);
    }
};

// Composite condition: AND of field predicates with type-erased storage
// Generates both in-memory filtering predicate and SQL QueryCondition
template<typename Entity>
class data_condition
{
    struct node {
        std::function<bool(const Entity&)> predicate;
        QueryCondition condition;
    };

    std::vector<node> m_nodes;

    template<typename Predicate>
    void add_predicate(Predicate&& pred)
    {
        m_nodes.push_back({
            [p = std::forward<Predicate>(pred)](const Entity& e) { return p.matches(e); },
            pred.to_query_condition()
        });
    }

public:
    data_condition() = default;

    template<typename... Predicates>
    explicit data_condition(Predicates&&... preds)
    {
        m_nodes.reserve(sizeof...(preds));
        (add_predicate(std::forward<Predicates>(preds)), ...);
    }

    bool empty() const { return m_nodes.empty(); }

    bool matches(const Entity& e) const
    {
        for (const auto& n : m_nodes)
            if (!n.predicate(e)) return false;
        return true;
    }

    QueryCondition to_query_condition() const
    {
        QueryCondition result;
        for (const auto& n : m_nodes)
            result = result.empty() ? n.condition : result.and_(n.condition);
        return result;
    }

    // Factory methods returning field_predicate values
    template<auto Field>
    static auto equal(std::decay_t<decltype(std::declval<Entity>().*Field)> value)
    { return field_predicate<Entity, Field, QueryOperator::Equal>(std::move(value)); }

    template<auto Field>
    static auto not_equal(std::decay_t<decltype(std::declval<Entity>().*Field)> value)
    { return field_predicate<Entity, Field, QueryOperator::NotEqual>(std::move(value)); }

    template<auto Field>
    static auto less(std::decay_t<decltype(std::declval<Entity>().*Field)> value)
    { return field_predicate<Entity, Field, QueryOperator::LessThan>(std::move(value)); }

    template<auto Field>
    static auto less_or_equal(std::decay_t<decltype(std::declval<Entity>().*Field)> value)
    { return field_predicate<Entity, Field, QueryOperator::LessThanOrEqual>(std::move(value)); }

    template<auto Field>
    static auto greater(std::decay_t<decltype(std::declval<Entity>().*Field)> value)
    { return field_predicate<Entity, Field, QueryOperator::GreaterThan>(std::move(value)); }

    template<auto Field>
    static auto greater_or_equal(std::decay_t<decltype(std::declval<Entity>().*Field)> value)
    { return field_predicate<Entity, Field, QueryOperator::GreaterThanOrEqual>(std::move(value)); }
};

} // namespace datahub

#endif // DATAHUB_DATA_CONDITION_HPP
