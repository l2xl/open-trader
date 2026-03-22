// Scratcher project
// Copyright (c) 2025 l2xl (l2xl/at/proton.me)
// Distributed under the Intellectual Property Reserve License (IPRL)
// -----BEGIN PGP PUBLIC KEY BLOCK-----
//
// mDMEYdxcVRYJKwYBBAHaRw8BAQdAfacBVThCP5QDPEgSbSIudtpJS4Y4Imm5dzaN
// lM1HTem0IkwyIFhsIChsMnhsKSA8bDJ4bEBwcm90b21tYWlsLmNvbT6IkAQTFggA
// OBYhBKRCfUyWnduCkisNl+WRcOaCK79JBQJh3FxVAhsDBQsJCAcCBhUKCQgLAgQW
// AgMBAh4BAheAAAoJEOWRcOaCK79JDl8A/0/AjYVbAURZJXP3tHRgZyYyN9txT6mW
// 0bYCcOf0rZ4NAQDoFX4dytPDvcjV7ovSQJ6dzvIoaRbKWGbHRCufrm5QBA==
// =KKu7
// -----END PGP PUBLIC KEY BLOCK-----

#ifndef  SCRATCHER_QUERY_BUILDER_HPP
#define  SCRATCHER_QUERY_BUILDER_HPP

#include <sstream>
#include <string>
#include <string_view>
#include <tuple>

namespace datahub {


/**
 * Query condition operators
 */
enum class QueryOperator {
    Equal,
    NotEqual,
    LessThan,
    LessThanOrEqual,
    GreaterThan,
    GreaterThanOrEqual,
    Like,
    In,
    Between,
    IsNull,
    IsNotNull
};

/**
 * Type-safe query condition builder
 */
class QueryCondition {
    std::string m_sql;
    size_t m_parameter_count;

public:
    QueryCondition() : m_parameter_count(0) {}
    
    // Static factory methods for creating conditions
    static QueryCondition where(const std::string& field, QueryOperator op) {
        std::string sql = field + " " + operator_to_sql(op) + " ?";
        return QueryCondition(std::move(sql), 1);
    }
    
    // Variadic template factory method for creating conditions with immediate values
    template<typename... Args>
    static QueryCondition where(const std::string& field, QueryOperator op, Args&&... args) {
        static_assert(sizeof...(args) == 1, "Single parameter condition requires exactly one argument");
        std::string sql = field + " " + operator_to_sql(op) + " ?";
        return QueryCondition(std::move(sql), 1);
    }
    
    static QueryCondition where_null(const std::string& field) {
        std::string sql = field + " IS NULL";
        return QueryCondition(std::move(sql), 0);
    }
    
    static QueryCondition where_not_null(const std::string& field) {
        std::string sql = field + " IS NOT NULL";
        return QueryCondition(std::move(sql), 0);
    }
    
    // Variadic template version for IN conditions
    template<typename... Args>
    static QueryCondition where_in(const std::string& field, Args&&... args) {
        static_assert(sizeof...(args) > 0, "IN condition requires at least one argument");
        
        std::string sql = field + " IN (";
        constexpr size_t arg_count = sizeof...(args);
        for (size_t i = 0; i < arg_count; ++i) {
            if (i > 0) sql += ", ";
            sql += "?";
        }
        sql += ")";
        
        return QueryCondition(std::move(sql), arg_count);
    }
    
    static QueryCondition where_between(const std::string& field) {
        std::string sql = field + " BETWEEN ? AND ?";
        return QueryCondition(std::move(sql), 2);
    }
    
    // Logical operators
    QueryCondition and_(const QueryCondition& other) const {
        return combine(other, true);
    }
    
    QueryCondition or_(const QueryCondition& other) const {
        return combine(other, false);
    }
    
    // Getters
    const std::string& sql() const { return m_sql; }
    size_t parameter_count() const { return m_parameter_count; }
    
    // Check if condition is empty
    bool empty() const { return m_sql.empty(); }

private:
    QueryCondition(std::string sql, size_t parameter_count) 
        : m_sql(std::move(sql)), m_parameter_count(parameter_count) {}
    
    QueryCondition combine(const QueryCondition& other, bool is_and) const {
        if (empty()) return other;
        if (other.empty()) return *this;
        
        std::string combined_sql = "(" + m_sql + ")" + (is_and ? " AND " : " OR ") + "(" + other.m_sql + ")";
        size_t combined_param_count = m_parameter_count + other.m_parameter_count;
        
        return QueryCondition(std::move(combined_sql), combined_param_count);
    }
    
    static std::string operator_to_sql(QueryOperator op) {
        switch (op) {
            case QueryOperator::Equal: return "=";
            case QueryOperator::NotEqual: return "!=";
            case QueryOperator::LessThan: return "<";
            case QueryOperator::LessThanOrEqual: return "<=";
            case QueryOperator::GreaterThan: return ">";
            case QueryOperator::GreaterThanOrEqual: return ">=";
            case QueryOperator::Like: return "LIKE";
            case QueryOperator::In: return "IN";
            case QueryOperator::Between: return "BETWEEN";
            case QueryOperator::IsNull: return "IS NULL";
            case QueryOperator::IsNotNull: return "IS NOT NULL";
            default: return "=";
        }
    }
};

namespace sql {
namespace detail {

template<typename Cols, size_t... Is>
std::string join_columns_impl(const Cols& cols, std::string_view sep, std::index_sequence<Is...>) {
    std::ostringstream result;
    ((result << (Is == 0 ? "" : sep) << std::get<Is>(cols)), ...);
    return result.str();
}

template<typename Cols>
inline std::string join_columns(const Cols& cols, std::string_view separator = ", ") {
    constexpr auto N = std::tuple_size_v<Cols>;
    if constexpr (N == 0) return "";
    else return join_columns_impl(cols, separator, std::make_index_sequence<N>{});
}

template<size_t... Is>
std::string generate_placeholders_impl(std::index_sequence<Is...>) {
    std::ostringstream result;
    ((result << (Is == 0 ? "" : ", ") << "?"), ...);
    return result.str();
}

template<size_t N>
inline std::string generate_placeholders() {
    if constexpr (N == 0) return "";
    else return generate_placeholders_impl(std::make_index_sequence<N>{});
}

} // namespace detail

inline std::string select_where(const std::string& table_name, const std::string& where_clause) {
    return where_clause.empty() ? ("SELECT * FROM " + table_name) : ("SELECT * FROM " + table_name + " WHERE " + where_clause);
}

inline std::string select_count_where(const std::string& table_name, const std::string& where_clause) {
    return where_clause.empty() ? "SELECT COUNT(*) FROM " + table_name : "SELECT COUNT(*) FROM " + table_name + " WHERE " + where_clause;
}

inline std::string delete_where(const std::string& table_name, const std::string& where_clause) {
    return "DELETE FROM " + table_name + " WHERE " + where_clause;
}

inline std::string drop_table(const std::string& table_name) {
    return "DROP TABLE IF EXISTS " + table_name;
}

template<typename Cols>
std::string insert(const std::string& table_name, const Cols& cols) {
    return "INSERT INTO " + table_name + " (" + detail::join_columns(cols) + ") VALUES (" + detail::generate_placeholders<std::tuple_size_v<Cols>>() + ")";
}

template<typename Cols>
std::string insert_or_replace(const std::string& table_name, const Cols& cols, std::string_view pk_name, size_t pk_index) {
    constexpr auto N = std::tuple_size_v<Cols>;
    std::ostringstream sql;
    sql << "INSERT INTO " << table_name << " (" << detail::join_columns(cols) << ") VALUES (" << detail::generate_placeholders<N>() << ")";
    sql << " ON CONFLICT(" << pk_name << ") DO UPDATE SET ";

    bool first = true;
    [&]<size_t... Is>(std::index_sequence<Is...>) {
        (([&]<size_t I>() {
            if (I != pk_index) {
                if (!first) sql << ", ";
                sql << std::get<I>(cols) << " = excluded." << std::get<I>(cols);
                first = false;
            }
        }.template operator()<Is>()), ...);
    }(std::make_index_sequence<N>{});

    sql << " WHERE ";
    first = true;
    [&]<size_t... Is>(std::index_sequence<Is...>) {
        (([&]<size_t I>() {
            if (I != pk_index) {
                if (!first) sql << " OR ";
                sql << table_name << "." << std::get<I>(cols) << " IS NOT excluded." << std::get<I>(cols);
                first = false;
            }
        }.template operator()<Is>()), ...);
    }(std::make_index_sequence<N>{});

    return sql.str();
}

template<typename Cols>
std::string update_where(const std::string& table_name, const Cols& cols, const std::string& where_clause) {
    constexpr auto N = std::tuple_size_v<Cols>;
    std::ostringstream sql;
    sql << "UPDATE " << table_name << " SET ";
    [&]<size_t... Is>(std::index_sequence<Is...>) {
        ((sql << (Is == 0 ? "" : ", ") << std::get<Is>(cols) << " = ?"), ...);
    }(std::make_index_sequence<N>{});
    sql << " WHERE " << where_clause;
    return sql.str();
}

template<typename Cols>
std::string create_table(const std::string& table_name, const Cols& col_defs) {
    return "CREATE TABLE IF NOT EXISTS " + table_name + " (" + detail::join_columns(col_defs) + ")";
}

} // namespace sql
} // namespace datahub

#endif //  SCRATCHER_QUERY_BUILDER_HPP
