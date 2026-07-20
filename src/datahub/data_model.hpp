// Open Trader
// Copyright (c) 2026 l2xl (l2xl/at/proton.me)
// Distributed under the Intellectual Property Reserve License, v2 (IPRL)

#ifndef SCRATCHER_DAO_HPP
#define SCRATCHER_DAO_HPP

#include <SQLiteCpp/SQLiteCpp.h>
#include <glaze/glaze.hpp>
#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/post.hpp>
#include <memory>
#include <algorithm>
#include <concepts>
#include <ranges>
#include <deque>

#include "query_builder.hpp"
#include "operations.hpp"
#include "metadata.hpp"

namespace datahub {

class QueryCondition;

/**
 * Generic DAO implementation providing CRUD operations for any entity type
 * Uses Glaze reflection for automatic schema generation and data mapping
 * Follows RAII principles - automatically creates metadata and table from Entity type
 * Uses operation classes for encapsulated database operations with precompiled statements
 */
template<typename Entity, auto PrimaryKey>
class data_model : public std::enable_shared_from_this<data_model<Entity, PrimaryKey> >
{
public:
    using entity_type = Entity;
    using cache_type = std::deque<entity_type>;
    using metadata_type = EntityMetadata<entity_type, PrimaryKey>;
    using strand_type = boost::asio::strand<boost::asio::any_io_executor>;

private:
    std::shared_ptr<SQLite::Database> m_db;
    metadata_type m_metadata;
    strand_type m_db_strand;

    struct EnsurePrivate {};
public:
    explicit data_model(std::shared_ptr<SQLite::Database> db, strand_type strand, std::string table_suffix, EnsurePrivate)
        : m_db(std::move(db))
        , m_metadata(metadata_type::Create(std::move(table_suffix)))
        , m_db_strand(std::move(strand))
    {
        if (!m_db) {
            throw std::invalid_argument("Database connection cannot be null");
        }
        m_db->exec("PRAGMA journal_mode=WAL");
        m_db->exec("PRAGMA synchronous=NORMAL");
        if (!m_db->tableExists(name())) {
            std::string sql = sql::create_table(m_metadata.table_name, m_metadata.column_definitions);
            m_db->exec(sql);
        }
    }

    static std::shared_ptr<data_model> create(std::shared_ptr<SQLite::Database> db, strand_type strand, std::string table_suffix = {})
    { return std::make_shared<data_model>(std::move(db), std::move(strand), std::move(table_suffix), EnsurePrivate{}); }

    const std::string& name() const
    { return m_metadata.table_name; }

    const SQLite::Database& database() const
    { return *m_db; }

    const metadata_type& metadata() const
    { return m_metadata; }

    template<std::convertible_to<Entity> T>
    void insert(const T &entity) {
        Insert<data_model> insert_op(this->shared_from_this());
        insert_op(static_cast<const Entity&>(entity));
    }

    template<std::convertible_to<Entity> T>
    bool insert_or_replace(const T &entity) {
        InsertOrReplace<data_model> insert_or_replace_op(this->shared_from_this());
        return insert_or_replace_op(static_cast<const Entity&>(entity));
    }

    template<typename... Args>
    std::deque<Entity> query(const QueryCondition &condition = {}, Args&&... args) {
        Query<data_model> query_op(this->shared_from_this(), condition);
        return query_op(std::forward<Args>(args)...);
    }

    template<std::convertible_to<Entity> T>
    void update(const T &entity) {
        Update<data_model> update_op(this->shared_from_this());
        update_op(static_cast<const Entity&>(entity));
    }

    template<typename... Args>
    void remove(const QueryCondition &condition = {}, Args&&... args) {
        if (condition.empty()) {
            throw std::invalid_argument("Delete condition cannot be empty");
        }
        Delete<data_model> delete_op(this->shared_from_this(), condition);
        delete_op(std::forward<Args>(args)...);
    }

    template<typename... Args>
    size_t count(const QueryCondition &condition = {}, Args&&... args) {
        Count<data_model> count_op(this->shared_from_this(), condition);
        return count_op(std::forward<Args>(args)...);
    }

    template <std::ranges::input_range Range, typename Container = cache_type>
    requires std::convertible_to<std::ranges::range_value_t<Range>, Entity>
    Container accept(Range&& entities)
    {
        Container new_entities;
        InsertOrReplace<data_model> op(this->shared_from_this());
        for (auto&& entity : std::forward<Range>(entities)) {
            if (op(static_cast<const Entity&>(entity)))
                new_entities.emplace_back(std::forward<decltype(entity)>(entity));
        }
        return new_entities;
    }

    template <std::ranges::input_range Range, typename Container>
    requires std::convertible_to<std::ranges::range_value_t<Range>, Entity>
    std::function<void(Range&&)> data_acceptor()
    {
        std::weak_ptr<data_model> ref = this->weak_from_this();
        return [ref](Range&& entities) {
            auto data = Container(std::ranges::begin(entities), std::ranges::end(entities));
            if (auto locked = ref.lock()) {
                boost::asio::post(locked->m_db_strand, [ref, data = std::move(data)]() mutable {
                    if (auto self = ref.lock())
                        self->template accept<Container, Container>(std::move(data));
                });
            }
        };
    }

    void drop_table() {
        if (m_db->tableExists(name())) {
            std::string sql = sql::drop_table(m_metadata.table_name);
            m_db->exec(sql);
        }
    }
};

} // namespace datahub

#endif // SCRATCHER_DAO_HPP
