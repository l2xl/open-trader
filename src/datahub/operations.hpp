// Open Trader
// Copyright (c) 2026 l2xl (l2xl/at/proton.me)
// Distributed under the Intellectual Property Reserve License, v2 (IPRL)

#ifndef SCRATCHER_DAO_OPERATIONS_HPP
#define SCRATCHER_DAO_OPERATIONS_HPP

#include "data_model.hpp"
#include "query_builder.hpp"
#include "metadata.hpp"
#include "currency.hpp"
#include <SQLiteCpp/SQLiteCpp.h>
#include <array>
#include <chrono>
#include <memory>
#include <utility>
#include <deque>

#include "sqlite3.h"

namespace datahub {


/**
 * Base class for all database operations
 * Encapsulates compiled SQLite statement and provides common functionality
 */
template<typename DAO>
class BaseOperation {
protected:
    std::shared_ptr<DAO> m_dao;
    SQLite::Statement m_statement;

public:
    template<typename SqlGen>
    explicit BaseOperation(std::shared_ptr<DAO> dao, SqlGen&& sql_gen)
        : m_dao(std::move(dao))
        , m_statement(m_dao->database(), std::invoke(std::forward<SqlGen>(sql_gen), *m_dao))
    {}

    virtual ~BaseOperation() = default;
    
    // Non-copyable but movable
    BaseOperation(const BaseOperation&) = delete;
    BaseOperation& operator=(const BaseOperation&) = delete;
    BaseOperation(BaseOperation&&) = default;
    BaseOperation& operator=(BaseOperation&&) = default;

protected:
    // Variadic template version for direct parameter binding
    template<typename... Args>
    void bind_parameters(Args&&... args) {
        m_statement.reset();
        bind_parameter_impl<1>(std::forward<Args>(args)...);
    }

    template<typename Entity>
    void bind_entity(const Entity& entity) {
        m_statement.reset();
        constexpr auto N = glz::reflect<Entity>::size;
        auto tie = glz::to_tie(entity);
        [&]<std::size_t... I>(std::index_sequence<I...>) {
            bind_parameter_impl<1>(glz::get<I>(tie)...);
        }(std::make_index_sequence<N>{});
    }
    
    // Bind a single parameter at the given index (1-based)
    template<typename T>
    void bind_parameter(int index, T&& value) {
        bind_single_parameter_dynamic(index, std::forward<T>(value));
    }

private:
    // Recursive template implementation for parameter binding
    template<int Index>
    void bind_parameter_impl() {
        // Base case - no more parameters to bind
    }
    
    template<int Index, typename T, typename... Rest>
    void bind_parameter_impl(T&& first, Rest&&... rest) {
        bind_single_parameter<Index>(std::forward<T>(first));
        bind_parameter_impl<Index + 1>(std::forward<Rest>(rest)...);
    }
    
    // Bind a single parameter at the given index
    template<int Index, typename T>
    void bind_single_parameter(T&& value) {
        using DecayedT = std::decay_t<T>;
        
        if constexpr (std::is_same_v<DecayedT, std::string>) {
            m_statement.bind(Index, std::forward<T>(value));
        } else if constexpr (std::is_same_v<DecayedT, const char*>) {
            m_statement.bind(Index, std::string(value));
        } else if constexpr (std::is_same_v<DecayedT, int64_t>) {
            m_statement.bind(Index, std::forward<T>(value));
        } else if constexpr (std::is_same_v<DecayedT, uint64_t>) {
            m_statement.bind(Index, static_cast<int64_t>(value));
        } else if constexpr (std::is_same_v<DecayedT, int>) {
            m_statement.bind(Index, static_cast<int64_t>(value));
        } else if constexpr (std::is_same_v<DecayedT, unsigned int>) {
            m_statement.bind(Index, static_cast<int64_t>(value));
        } else if constexpr (std::is_same_v<DecayedT, double>) {
            m_statement.bind(Index, std::forward<T>(value));
        } else if constexpr (std::is_same_v<DecayedT, float>) {
            m_statement.bind(Index, static_cast<double>(value));
        } else if constexpr (std::is_same_v<DecayedT, bool>) {
            m_statement.bind(Index, value ? 1 : 0);
        } else if constexpr (std::is_enum_v<DecayedT>) {
            m_statement.bind(Index, static_cast<int64_t>(value));
        } else if constexpr (scratcher::is_currency_v<DecayedT>) {
            m_statement.bind(Index, value.to_string());
        } else if constexpr (detail::is_time_point_v<DecayedT>) {
            m_statement.bind(Index, static_cast<int64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(value.time_since_epoch()).count()));
        } else if constexpr (std::is_same_v<DecayedT, std::monostate>) {
            m_statement.bind(Index); // Bind NULL
        } else if constexpr (requires { typename DecayedT::value_type; } && std::is_same_v<DecayedT, std::optional<typename DecayedT::value_type>>) {
            // Handle std::optional types
            if (value.has_value()) {
                bind_single_parameter<Index>(value.value());
            } else {
                m_statement.bind(Index); // Bind NULL
            }
        } else {
            static_assert(sizeof(T) == 0, "Unsupported parameter type for bind_parameters");
        }
    }
    
    // Runtime version of bind_single_parameter
    template<typename T>
    void bind_single_parameter_dynamic(int index, T&& value) {
        using DecayedT = std::decay_t<T>;
        
        if constexpr (std::is_same_v<DecayedT, std::string>) {
            m_statement.bind(index, std::forward<T>(value));
        } else if constexpr (std::is_same_v<DecayedT, const char*>) {
            m_statement.bind(index, std::string(value));
        } else if constexpr (std::is_same_v<DecayedT, int64_t>) {
            m_statement.bind(index, std::forward<T>(value));
        } else if constexpr (std::is_same_v<DecayedT, uint64_t>) {
            m_statement.bind(index, static_cast<int64_t>(value));
        } else if constexpr (std::is_same_v<DecayedT, int>) {
            m_statement.bind(index, static_cast<int64_t>(value));
        } else if constexpr (std::is_same_v<DecayedT, unsigned int>) {
            m_statement.bind(index, static_cast<int64_t>(value));
        } else if constexpr (std::is_same_v<DecayedT, double>) {
            m_statement.bind(index, std::forward<T>(value));
        } else if constexpr (std::is_same_v<DecayedT, float>) {
            m_statement.bind(index, static_cast<double>(value));
        } else if constexpr (std::is_same_v<DecayedT, bool>) {
            m_statement.bind(index, value ? 1 : 0);
        } else if constexpr (std::is_enum_v<DecayedT>) {
            m_statement.bind(index, static_cast<int64_t>(value));
        } else if constexpr (scratcher::is_currency_v<DecayedT>) {
            m_statement.bind(index, value.to_string());
        } else if constexpr (detail::is_time_point_v<DecayedT>) {
            m_statement.bind(index, static_cast<int64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(value.time_since_epoch()).count()));
        } else if constexpr (std::is_same_v<DecayedT, std::monostate>) {
            m_statement.bind(index); // Bind NULL
        } else if constexpr (requires { typename DecayedT::value_type; } && std::is_same_v<DecayedT, std::optional<typename DecayedT::value_type>>) {
            // Handle std::optional types
            if (value.has_value()) {
                bind_single_parameter_dynamic(index, value.value());
            } else {
                m_statement.bind(index); // Bind NULL
            }
        } else {
            static_assert(sizeof(T) == 0, "Unsupported parameter type for bind_parameters");
        }
    }
};

/**
 * Insert operation - compiles INSERT statement in constructor
 */
template<typename DAO>
class Insert : public BaseOperation<DAO> {

public:
    explicit Insert(std::shared_ptr<DAO> dao)
        : BaseOperation<DAO>(std::move(dao), [](const DAO& d) { return sql::insert(d.name(), DAO::metadata_type::column_names()); })
    {}

    // Execute single entity insert - reuses precompiled statement
    void operator()(const typename DAO::entity_type& entity) {
        BaseOperation<DAO>::bind_entity(entity);
        BaseOperation<DAO>::m_statement.exec();
    }
};

/**
 * InsertOrReplace operation - compiles INSERT ON CONFLICT DO UPDATE WHERE statement in constructor
 * Returns true if data was inserted or updated, false if data was unchanged
 */
template<typename DAO>
class InsertOrReplace : public BaseOperation<DAO> {

public:
    explicit InsertOrReplace(std::shared_ptr<DAO> dao)
        : BaseOperation<DAO>(std::move(dao), [](const DAO& d) { return sql::insert_or_replace(d.name(), DAO::metadata_type::column_names(), DAO::metadata_type::primary_key_name, DAO::metadata_type::primary_key_index); })
    {}

    // Execute single entity insert or replace - reuses precompiled statement
    // Returns true if data was modified (inserted or updated), false if unchanged
    bool operator()(const typename DAO::entity_type& entity) {
        BaseOperation<DAO>::bind_entity(entity);
        BaseOperation<DAO>::m_statement.exec();

        // Get the number of rows modified (0 if WHERE clause prevented update)
        sqlite3* db_handle = const_cast<SQLite::Database&>(BaseOperation<DAO>::m_dao->database()).getHandle();
        int changes = sqlite3_changes(db_handle);

        // changes > 0 means either INSERT (1) or UPDATE (1) happened
        // changes == 0 means WHERE clause prevented update (data unchanged)
        return changes > 0;
    }
};

/**
 * Query operation - compiles SELECT statement in constructor
 */
template<typename DAO>
class Query : public BaseOperation<DAO> {
    size_t m_required_param_count;
    
public:
    // Constructor for SELECT * FROM table WHERE condition
    Query(std::shared_ptr<DAO> dao, const QueryCondition& condition = {})
        : BaseOperation<DAO>(std::move(dao), [&condition](const DAO& d) { return sql::select_where(d.name(), condition.sql()); })
        , m_required_param_count(condition.parameter_count()) {}
    
    // Execute query - accepts variable arguments pack
    template<typename... Args>
    std::deque<typename DAO::entity_type> operator()(Args&&... args) {
        if (sizeof...(args) != m_required_param_count) {
            throw std::logic_error("Query requires exactly " + std::to_string(m_required_param_count) + " parameters, got " + std::to_string(sizeof...(args)));
        }
        return execute_query(std::forward<Args>(args)...);
    }
    
private:
    template<typename... Args>
    std::deque<typename DAO::entity_type> execute_query(Args&&... args) {
        std::deque<typename DAO::entity_type> results;
        BaseOperation<DAO>::bind_parameters(std::forward<Args>(args)...);
        
        while (BaseOperation<DAO>::m_statement.executeStep()) {
            results.push_back(create_entity_from_statement());
        }
        
        return results;
    }

    typename DAO::entity_type create_entity_from_statement() {
        constexpr auto fc = DAO::metadata_type::field_count;
        typename DAO::entity_type entity{};
        auto tie = glz::to_tie(entity);

        [&]<std::size_t... I>(std::index_sequence<I...>) {
            (([&]<std::size_t Idx>() {
                using FieldType = std::decay_t<decltype(glz::get<Idx>(tie))>;
                auto col = BaseOperation<DAO>::m_statement.getColumn(static_cast<int>(Idx));
                if (col.isNull()) {
                    glz::get<Idx>(tie) = FieldType{};
                } else if constexpr (std::is_same_v<FieldType, std::string>) {
                    glz::get<Idx>(tie) = col.getString();
                } else if constexpr (std::is_same_v<FieldType, bool>) {
                    glz::get<Idx>(tie) = col.getInt64() != 0;
                } else if constexpr (std::is_enum_v<FieldType>) {
                    glz::get<Idx>(tie) = static_cast<FieldType>(col.getInt64());
                } else if constexpr (std::is_integral_v<FieldType>) {
                    glz::get<Idx>(tie) = static_cast<FieldType>(col.getInt64());
                } else if constexpr (std::is_floating_point_v<FieldType>) {
                    glz::get<Idx>(tie) = static_cast<FieldType>(col.getDouble());
                } else if constexpr (scratcher::is_currency_v<FieldType>) {
                    auto s = col.getString();
                    glz::get<Idx>(tie) = s.empty() ? FieldType{} : FieldType(s);
                } else if constexpr (detail::is_time_point_v<FieldType>) {
                    glz::get<Idx>(tie) = FieldType{std::chrono::milliseconds{col.getInt64()}};
                } else if constexpr (requires { typename FieldType::value_type; } && std::is_same_v<FieldType, std::optional<typename FieldType::value_type>>) {
                    using InnerType = typename FieldType::value_type;
                    if constexpr (std::is_same_v<InnerType, std::string>) {
                        glz::get<Idx>(tie) = FieldType{col.getString()};
                    } else if constexpr (std::is_integral_v<InnerType>) {
                        glz::get<Idx>(tie) = FieldType{static_cast<InnerType>(col.getInt64())};
                    } else if constexpr (std::is_floating_point_v<InnerType>) {
                        glz::get<Idx>(tie) = FieldType{static_cast<InnerType>(col.getDouble())};
                    } else if constexpr (std::is_enum_v<InnerType>) {
                        glz::get<Idx>(tie) = FieldType{static_cast<InnerType>(col.getInt64())};
                    } else if constexpr (scratcher::is_currency_v<InnerType>) {
                        auto s = col.getString();
                        glz::get<Idx>(tie) = s.empty() ? FieldType{} : FieldType{InnerType(s)};
                    }
                }
            }.template operator()<I>()), ...);
        }(std::make_index_sequence<fc>{});

        return entity;
    }
};


/**
 * Update operation - compiles UPDATE statement in constructor
 * Updates entity by primary key (extracted from entity itself)
 */
template<typename DAO>
class Update : public BaseOperation<DAO> {
    static constexpr auto field_count = DAO::metadata_type::field_count;

    using BaseOperation<DAO>::m_dao;
public:
    explicit Update(std::shared_ptr<DAO> dao)
        : BaseOperation<DAO>(std::move(dao), [](const DAO& d) {
            constexpr auto names = DAO::metadata_type::column_names();
            std::array<std::string_view, field_count - 1> non_pk_cols;
            size_t j = 0;
            [&]<std::size_t... I>(std::index_sequence<I...>) {
                (([&]<std::size_t Idx>() {
                    if (Idx != DAO::metadata_type::primary_key_index) {
                        non_pk_cols[j++] = std::get<Idx>(names);
                    }
                }.template operator()<I>()), ...);
            }(std::make_index_sequence<field_count>{});
            std::string where_clause = std::string(DAO::metadata_type::primary_key_name) + " = ?";
            return sql::update_where(d.name(), non_pk_cols, where_clause);
        })
    {}

    // Execute update using primary key from entity
    void operator()(const typename DAO::entity_type& entity) {
        auto all_values = glz::to_tie(entity);

        // Reset statement before binding
        BaseOperation<DAO>::m_statement.reset();

        int param_index = 1;
        [&]<std::size_t... I>(std::index_sequence<I...>) {
            (([&]<std::size_t Idx>() {
                if (Idx != DAO::metadata_type::primary_key_index) {
                    BaseOperation<DAO>::bind_parameter(param_index++, glz::get<Idx>(all_values));
                }
            }.template operator()<I>()), ...);
        }(std::make_index_sequence<field_count>{});

        [&]<std::size_t... I>(std::index_sequence<I...>) {
            (([&]<std::size_t Idx>() {
                if (Idx == DAO::metadata_type::primary_key_index) {
                    BaseOperation<DAO>::bind_parameter(param_index, glz::get<Idx>(all_values));
                }
            }.template operator()<I>()), ...);
        }(std::make_index_sequence<field_count>{});

        BaseOperation<DAO>::m_statement.exec();
    }
};

/**
 * Delete operation - compiles DELETE statement in constructor
 */
template<typename DAO>
class Delete : public BaseOperation<DAO> {
    size_t m_required_param_count;
    
public:
    // Constructor compiles DELETE statement once with condition
    Delete(std::shared_ptr<DAO> dao, const QueryCondition& condition = {})
        : BaseOperation<DAO>(std::move(dao), [&condition](const DAO& d) { return sql::delete_where(d.name(), condition.sql()); })
        , m_required_param_count(condition.parameter_count())
    {}
    
    // Execute delete with variable arguments pack - reuses precompiled statement
    template<typename... Args>
    void operator()(Args&&... args) {
        if (sizeof...(args) != m_required_param_count) {
            throw std::logic_error("Delete requires exactly " + std::to_string(m_required_param_count) + " parameters, got " + std::to_string(sizeof...(args)));
        }
        BaseOperation<DAO>::bind_parameters(std::forward<Args>(args)...);
        BaseOperation<DAO>::m_statement.exec();
    }
};

/**
 * Count operation - compiles SELECT COUNT(*) statement in constructor
 */
template<typename DAO>
class Count : public BaseOperation<DAO> {
    size_t m_required_param_count;
    
public:
    // Constructor for COUNT(*) WHERE condition - compiles statement once
    Count(std::shared_ptr<DAO> dao, const QueryCondition& condition = {})
        : BaseOperation<DAO>(std::move(dao), [&condition](const DAO& d) { return sql::select_count_where(d.name(), condition.sql()); })
        , m_required_param_count(condition.parameter_count()) {}
    
    // Execute count query - accepts variable arguments pack
    template<typename... Args>
    size_t operator()(Args&&... args) {
        if (sizeof...(args) != m_required_param_count) {
            throw std::logic_error("Count requires exactly " + std::to_string(m_required_param_count) + " parameters, got " + std::to_string(sizeof...(args)));
        }
        return execute_count(std::forward<Args>(args)...);
    }
    
private:
    template<typename... Args>
    size_t execute_count(Args&&... args) {
        BaseOperation<DAO>::bind_parameters(std::forward<Args>(args)...);
        
        if (BaseOperation<DAO>::m_statement.executeStep()) {
            return static_cast<size_t>(BaseOperation<DAO>::m_statement.getColumn(0).getInt64());
        }
        
        return 0;
    }
};

/**
 * Transaction class following RAII and builder pattern
 * Collects operations and executes them atomically in operator()
 */
// class Transaction {
//     std::shared_ptr<SQLite::Database> m_db;
//     std::vector<std::function<void()>> m_operations;
//     bool m_executed = false;
//
// public:
//     explicit Transaction(std::shared_ptr<SQLite::Database> db)
//         : m_db(std::move(db)) {}
//
//     // Non-copyable but movable
//     Transaction(const Transaction&) = delete;
//     Transaction& operator=(const Transaction&) = delete;
//     Transaction(Transaction&&) = default;
//     Transaction& operator=(Transaction&&) = default;
//
//     // Add operation to transaction
//     template<typename Operation, typename... Args>
//     Transaction& add(Operation&& operation, Args&&... args) {
//         if (m_executed) {
//             throw std::logic_error("Cannot add operations to already executed transaction");
//         }
//
//         m_operations.emplace_back([op = std::forward<Operation>(operation), args...]() mutable {
//             op(args...);
//         });
//
//         return *this;
//     }
//
//     // Execute all operations in transaction
//     void operator()() {
//         if (m_executed) {
//             throw std::logic_error("Transaction already executed");
//         }
//
//         SQLite::Transaction transaction(*m_db);
//
//         try {
//             for (auto& operation : m_operations) {
//                 operation();
//             }
//             transaction.commit();
//             m_executed = true;
//         } catch (...) {
//             // Transaction automatically rolls back on destruction if not committed
//             m_executed = false;
//             throw;
//         }
//     }
//
//     bool is_executed() const { return m_executed; }
// };

} // namespace datahub

#endif // SCRATCHER_DAO_OPERATIONS_HPP
