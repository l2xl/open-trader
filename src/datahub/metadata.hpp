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

#ifndef SCRATCHER_DAO_METADATA_HPP
#define SCRATCHER_DAO_METADATA_HPP

#include "query_builder.hpp"
#include <glaze/glaze.hpp>
#include <type_traits>
#include <algorithm>
#include <array>

namespace datahub {

// Glaze-based reflection utilities
namespace detail {


template<typename Entity, auto MemberPtr>
constexpr std::size_t find_member_index() {
    constexpr auto field_count = glz::reflect<Entity>::size;

    Entity dummy{};
    auto tie = glz::to_tie(dummy);
    auto* target_addr = &(dummy.*MemberPtr);

    std::size_t result = 0;
    [&]<std::size_t... I>(std::index_sequence<I...>) {
        auto check_field = [&]<std::size_t Idx>() {
            if (static_cast<const void*>(&glz::get<Idx>(tie)) == static_cast<const void*>(target_addr)) {
                result = Idx;
            }
        };
        (check_field.template operator()<I>(), ...);
    }(std::make_index_sequence<field_count>{});

    return result;
}



// Generate SQL column type from C++ type
template<typename T>
std::string get_sql_type()
{
    if constexpr (std::is_same_v<T, std::string>) {
        return "TEXT";
    } else if constexpr (std::is_integral_v<T>) {
        return "INTEGER";
    } else if constexpr (std::is_floating_point_v<T>) {
        return "REAL";
    } else if constexpr (std::is_same_v<T, bool>) {
        return "INTEGER"; // SQLite stores booleans as integers
    } else if constexpr (std::is_enum_v<T>){
        return "INTEGER";
    } else if constexpr (requires { typename T::value_type; } && std::is_same_v<T, std::optional<typename T::value_type>>) {
        // Handle std::optional types - use the underlying type
        return get_sql_type<typename T::value_type>();
    } else {
        return "TEXT"; // Default to TEXT for complex types
    }
}

} // namespace detail

/**
 * Entity metadata interface for schema generation and field access
 *
 * @tparam PrimaryKey Pointer to member field that should be the primary key
 *                       If nullptr, the first field will be used as primary key
 */
template<typename Entity, auto PrimaryKey>
class EntityMetadata
{
    struct EnsurePrivate {};
public:
    static constexpr auto field_count = glz::reflect<Entity>::size;

    EntityMetadata(EnsurePrivate) {}

    EntityMetadata(const EntityMetadata& entity) = default;
    EntityMetadata(EntityMetadata&& entity) = default;

    EntityMetadata& operator=(const EntityMetadata& entity) = default;
    EntityMetadata& operator=(EntityMetadata&& entity) = default;

    static constexpr std::size_t primary_key_index = detail::find_member_index<Entity, PrimaryKey>();
    static constexpr std::string_view primary_key_name = glz::member_names<Entity>[primary_key_index];

    std::string table_name;
    std::array<std::string, field_count> column_definitions;

    static constexpr auto column_names() { return glz::member_names<Entity>; }

    static EntityMetadata Create(std::string table_suffix = {})
    {
        EntityMetadata metadata(EnsurePrivate{});

        constexpr auto field_names = glz::member_names<Entity>;

        constexpr auto type_name = glz::type_name<Entity>;
        metadata.table_name = std::string(type_name);

        std::transform(metadata.table_name.begin(), metadata.table_name.end(), metadata.table_name.begin(), ::tolower);
        std::replace(metadata.table_name.begin(), metadata.table_name.end(), ':', '_');

        if (!table_suffix.empty()) {
            metadata.table_name += "_" + table_suffix;
        }

        Entity dummy{};
        auto tie = glz::to_tie(dummy);

        [&]<std::size_t... I>(std::index_sequence<I...>) {
            auto process_field = [&]<std::size_t Idx>() {
                std::string field_name{field_names[Idx]};
                using FieldType = std::decay_t<decltype(glz::get<Idx>(tie))>;

                std::string column_def = field_name + " " + detail::get_sql_type<FieldType>();
                if (primary_key_index == Idx) {
                    column_def += " PRIMARY KEY";
                }
                metadata.column_definitions[Idx] = std::move(column_def);
            };
            (process_field.template operator()<I>(), ...);
        }(std::make_index_sequence<field_count>{});


        return metadata;
    }
};

}

#endif //SCRATCHER_DAO_METADATA_HPP
