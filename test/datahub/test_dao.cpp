// XCockpit
// Copyright (c) 2026 l2xl (l2xl/at/proton.me)
// Distributed under the Intellectual Property Reserve License, v2 (IPRL)

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_session.hpp>

#include <memory>
#include <iostream>
#include <SQLiteCpp/SQLiteCpp.h>

#include "../../src/datahub/data_model.hpp"
#include "scheduler.hpp"
#include "data/bybit/entities/fee_rate.hpp"
#include "data/bybit/entities/public_trade.hpp"

using namespace datahub;
using namespace scratcher::bybit;

// Helper to create an in-memory database for testing
class TestDatabase {
public:
    TestDatabase()
        : db(std::make_shared<SQLite::Database>(":memory:", SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE))
        , sched(scratcher::scheduler::create(1))
    {
    }

    // data_model::create needs a strand; the DAO operations exercised here are
    // synchronous, so the io_context is never run — the strand only has to be a
    // valid object bound to a live executor.
    auto strand() { return boost::asio::make_strand(sched->io().get_executor()); }

    std::shared_ptr<SQLite::Database> db;
    std::shared_ptr<scratcher::scheduler> sched;
};

TEST_CASE("Empty database", "[dao][DATAHUB-011]") {
    TestDatabase test_db;
    
    SECTION("Feerate") {
        // Create DAO with symbol as primary key - table created automatically in constructor
        auto dao = data_model<FeeRate, &FeeRate::symbol>::create(test_db.db, test_db.strand());

        // Query empty database - should return no records
        auto all_records = dao->query();
        CHECK(all_records.empty());

        // Count should be 0
        auto count = dao->count();
        CHECK(count == 0);
    }

    SECTION("PublicTrade")
    {
        // Create DAO with symbol as primary key - table created automatically in constructor
        auto dao = data_model<PublicTrade, &PublicTrade::execId>::create(test_db.db, test_db.strand());

        // Query empty database - should return no records
        auto all_records = dao->query();
        CHECK(all_records.empty());

        // Count should be 0
        auto count = dao->count();
        CHECK(count == 0);
    }
}

TEST_CASE("Insert", "[dao][fee_rate][DATAHUB-012]") {
    TestDatabase test_db;
    
    // Create DAO with symbol as primary key - table created automatically in constructor
    auto dao = data_model<FeeRate, &FeeRate::symbol>::create(test_db.db, test_db.strand());
    
    // ByBit API example JSON from documentation
    std::string bybit_json = R"({
        "symbol": "ETHUSDT",
        "takerFeeRate": "0.0006",
        "makerFeeRate": "0.0001"
    })";
    
    // Parse JSON into FeeRate object using Glaze
    auto parse_result = glz::read_json<FeeRate>(bybit_json);
    REQUIRE(parse_result.has_value());
    
    FeeRate fee_rate = parse_result.value();
    
    // Verify parsed data
    CHECK(fee_rate.symbol == "ETHUSDT");
    CHECK(fee_rate.takerFeeRate.to_string() == "0.0006");
    CHECK(fee_rate.makerFeeRate.to_string() == "0.0001");
    CHECK_FALSE(fee_rate.baseCoin.has_value()); // Should be empty in this example
    
    // Insert the record - synchronous operation
    dao->insert(fee_rate);
    
    // Verify insertion - count should be 1
    auto count = dao->count();
    CHECK(count == 1);
    
    // Query all records
    auto all_records = dao->query();
    REQUIRE(all_records.size() == 1);
    
    const auto& retrieved_record = all_records[0];
    CHECK(retrieved_record.symbol == "ETHUSDT");
    CHECK(retrieved_record.takerFeeRate.to_string() == "0.0006");
    CHECK(retrieved_record.makerFeeRate.to_string() == "0.0001");
    CHECK_FALSE(retrieved_record.baseCoin.has_value());
}

TEST_CASE("Query methods", "[dao][fee_rate][DATAHUB-013]") {
    TestDatabase test_db;
    
    // Create DAO with symbol as primary key - table created automatically in constructor
    auto dao = data_model<FeeRate, &FeeRate::symbol>::create(test_db.db, test_db.strand());
    
    // Create test data - multiple fee rates
    std::vector<FeeRate> test_data = {
        {
            std::string{"ETHUSDT"},   // symbol (primary key)
            std::nullopt,             // baseCoin
            currency<uint64_t>("0.0006"),  // takerFeeRate
            currency<uint64_t>("0.0001")   // makerFeeRate
        },
        {
            std::string{"BTCUSDT"},   // symbol (primary key)
            std::string{"BTC"},       // baseCoin
            currency<uint64_t>("0.0008"),  // takerFeeRate
            currency<uint64_t>("0.0002")   // makerFeeRate
        },
        {
            std::string{"SOLUSDT"},   // symbol (primary key)
            std::string{"SOL"},       // baseCoin
            currency<uint64_t>("0.0010"),  // takerFeeRate
            currency<uint64_t>("0.0005")   // makerFeeRate
        }
    };
    
    // Insert all test data - synchronous operations
    for (const auto& fee_rate : test_data) {
        dao->insert(fee_rate);
    }
    
    SECTION("queryAll() - Returns all records") {
        auto all_records = dao->query();
        CHECK(all_records.size() == 3);
    }
    
    SECTION("query() - Find specific record by symbol") {
        auto eth_condition = QueryCondition::where("symbol", QueryOperator::Equal);
        auto eth_records = dao->query(eth_condition, "ETHUSDT");
        REQUIRE(eth_records.size() == 1);
        const auto& eth_record = eth_records[0];
        CHECK(eth_record.symbol == "ETHUSDT");
        CHECK(eth_record.takerFeeRate.to_string() == "0.0006");

        auto btc_condition = QueryCondition::where("symbol", QueryOperator::Equal);
        auto btc_records = dao->query(btc_condition, "BTCUSDT");
        REQUIRE(btc_records.size() == 1);
        const auto& btc_record = btc_records[0];
        CHECK(btc_record.symbol == "BTCUSDT");
        CHECK(btc_record.takerFeeRate.to_string() == "0.0008");
    }

    SECTION("count() with condition - Check record existence") {
        auto eth_condition = QueryCondition::where("symbol", QueryOperator::Equal);
        auto eth_count = dao->count(eth_condition, "ETHUSDT");
        CHECK(eth_count == 1);

        auto nonexistent_condition = QueryCondition::where("symbol", QueryOperator::Equal);
        auto nonexistent_count = dao->count(nonexistent_condition, "NONEXISTENT");
        CHECK(nonexistent_count == 0);
    }

    SECTION("count() - Total count") {
        auto total_count = dao->count();
        CHECK(total_count == 3);
    }
    
    SECTION("update() - Update existing record") {
        // Get ETHUSDT record
        auto eth_condition = QueryCondition::where("symbol", QueryOperator::Equal);
        auto eth_records = dao->query(eth_condition, "ETHUSDT");
        REQUIRE(eth_records.size() == 1);
        auto eth_record = eth_records[0];
        
        // Modify fee rates
        eth_record.takerFeeRate = currency<uint64_t>("0.0007");
        eth_record.makerFeeRate = currency<uint64_t>("0.0002");
        
        // Update record - synchronous operation (by primary key)
        dao->update(eth_record);
        
        // Verify update
        auto updated_records = dao->query(eth_condition, "ETHUSDT");
        REQUIRE(updated_records.size() == 1);
        const auto& updated_record = updated_records[0];
        CHECK(updated_record.takerFeeRate.to_string() == "0.0007");
        CHECK(updated_record.makerFeeRate.to_string() == "0.0002");
    }
    
    SECTION("remove() - Delete specific record") {
        // Remove SOLUSDT
        auto sol_condition = QueryCondition::where("symbol", QueryOperator::Equal);
        dao->remove(sol_condition, "SOLUSDT");

        // Verify removal
        auto sol_count = dao->count(sol_condition, "SOLUSDT");
        CHECK(sol_count == 0);

        auto remaining_count = dao->count();
        CHECK(remaining_count == 2);
    }
}

TEST_CASE("Optional fields", "[dao][fee_rate][DATAHUB-014]") {
    TestDatabase test_db;
    
    // Create DAO with symbol as primary key - table created automatically in constructor
    auto dao = data_model<FeeRate, &FeeRate::symbol>::create(test_db.db, test_db.strand());

    // ByBit API example with baseCoin (for futures/derivatives)
    std::string bybit_json_with_base = R"({
        "symbol": "BTCUSDT",
        "baseCoin": "BTC",
        "takerFeeRate": "0.0008",
        "makerFeeRate": "0.0002"
    })";
    
    // Parse JSON into FeeRate object using Glaze
    auto parse_result = glz::read_json<FeeRate>(bybit_json_with_base);
    REQUIRE(parse_result.has_value());
    
    FeeRate fee_rate = parse_result.value();
    
    // Verify parsed data
    CHECK(fee_rate.symbol == "BTCUSDT");
    REQUIRE(fee_rate.baseCoin.has_value());
    CHECK(fee_rate.baseCoin.value() == "BTC");
    CHECK(fee_rate.takerFeeRate.to_string() == "0.0008");
    CHECK(fee_rate.makerFeeRate.to_string() == "0.0002");
    
    // Insert and verify - synchronous operation
    dao->insert(fee_rate);
    
    auto btc_condition = QueryCondition::where("symbol", QueryOperator::Equal);
    auto retrieved_records = dao->query(btc_condition, "BTCUSDT");
    REQUIRE(retrieved_records.size() == 1);
    const auto& retrieved = retrieved_records[0];
    REQUIRE(retrieved.baseCoin.has_value());
    CHECK(retrieved.baseCoin.value() == "BTC");
}

TEST_CASE("Batch insert", "[dao][fee_rate][DATAHUB-015]") {
    TestDatabase test_db;
    
    // Create DAO with symbol as primary key - table created automatically in constructor
    auto dao = data_model<FeeRate, &FeeRate::symbol>::create(test_db.db, test_db.strand());
    
    // Create test data - multiple fee rates
    std::vector<FeeRate> test_data = {
        {
            std::string{"ETHUSDT"},   // symbol (primary key)
            std::nullopt,             // baseCoin
            currency<uint64_t>("0.0006"),  // takerFeeRate
            currency<uint64_t>("0.0001")   // makerFeeRate
        },
        {
            std::string{"BTCUSDT"},   // symbol (primary key)
            std::string{"BTC"},       // baseCoin
            currency<uint64_t>("0.0008"),  // takerFeeRate
            currency<uint64_t>("0.0002")   // makerFeeRate
        },
        {
            std::string{"SOLUSDT"},   // symbol (primary key)
            std::string{"SOL"},       // baseCoin
            currency<uint64_t>("0.0010"),  // takerFeeRate
            currency<uint64_t>("0.0005")   // makerFeeRate
        }
    };
    
    // Insert all test data in batch - synchronous operation
    dao->insert(test_data[0]);
    dao->insert(test_data[1]);
    dao->insert(test_data[2]);

    // Verify all records were inserted
    auto all_records = dao->query();
    CHECK(all_records.size() == 3);
    
    auto total_count = dao->count();
    CHECK(total_count == 3);
}

// TEST_CASE("Transaction", "[dao][fee_rate][transaction]") {
//     TestDatabase test_db;
//
//     // Create DAO with symbol as primary key - table created automatically in constructor
//     auto dao = data_model<FeeRate>::create(test_db.db, test_db.strand());
//
//     // Create test data
//     std::vector<FeeRate> test_data = {
//         {
//             std::string{"ETHUSDT"},   // symbol (primary key)
//             std::nullopt,             // baseCoin
//             "0.0006",                 // takerFeeRate
//             "0.0001"                  // makerFeeRate
//         },
//         {
//             std::string{"BTCUSDT"},   // symbol (primary key)
//             std::string{"BTC"},       // baseCoin
//             "0.0008",                 // takerFeeRate
//             "0.0002"                  // makerFeeRate
//         }
//     };
//
//     SECTION("Successful transaction") {
//         // Create transaction and add operations
//         auto transaction = dao->createTransaction();
//
//         // Create operations
//         Insert<FeeRate> insert_op1(dao);
//         Insert<FeeRate> insert_op2(dao);
//
//         // Add operations to transaction
//         transaction.add(std::move(insert_op1), test_data[0]);
//         transaction.add(std::move(insert_op2), test_data[1]);
//
//         // Execute transaction
//         transaction();
//
//         // Verify both records were inserted
//         auto all_records = dao->queryAll();
//         CHECK(all_records.size() == 2);
//         CHECK(transaction.is_executed());
//     }
//
//     SECTION("Transaction rollback on error") {
//         // Insert first record normally
//         dao->insert(test_data[0]);
//
//         // Create transaction that will fail
//         auto transaction = dao->createTransaction();
//
//         // Create operations - second insert will fail due to primary key constraint
//         Insert<FeeRate> insert_op1(dao);
//         Insert<FeeRate> insert_op2(dao);
//
//         // Add operations to transaction (second one will fail)
//         transaction.add(std::move(insert_op1), test_data[1]);
//         transaction.add(std::move(insert_op2), test_data[0]); // Duplicate primary key
//
//         // Execute transaction - should throw and rollback
//         REQUIRE_THROWS(transaction());
//
//         // Verify only the first record exists (transaction rolled back)
//         auto all_records = dao->queryAll();
//         CHECK(all_records.size() == 1);
//         CHECK(all_records[0].symbol == "ETHUSDT");
//         CHECK_FALSE(transaction.is_executed());
//     }
// }

TEST_CASE("InsertOrReplace with change detection", "[dao][fee_rate][insert_or_replace][DATAHUB-016]") {
    TestDatabase test_db;

    // Create DAO with symbol as primary key - table created automatically in constructor
    auto dao = data_model<FeeRate, &FeeRate::symbol>::create(test_db.db, test_db.strand());

    // Create test data
    FeeRate test_fee_rate{
        std::string{"ETHUSDT"},   // symbol (primary key)
        std::nullopt,             // baseCoin
        currency<uint64_t>("0.0006"),  // takerFeeRate
        currency<uint64_t>("0.0001")   // makerFeeRate
    };

    SECTION("First insert - should return true (data modified)") {
        bool modified = dao->insert_or_replace(test_fee_rate);
        CHECK(modified == true);

        // Verify insertion
        auto count = dao->count();
        CHECK(count == 1);
    }

    SECTION("Second insert with same data - should return false (data unchanged)") {
        // First insert
        bool first_modified = dao->insert_or_replace(test_fee_rate);
        CHECK(first_modified == true);

        // Second insert with identical data
        bool second_modified = dao->insert_or_replace(test_fee_rate);
        CHECK(second_modified == false);

        // Verify still only one record
        auto count = dao->count();
        CHECK(count == 1);
    }

    SECTION("Update with different data - should return true (data modified)") {
        // First insert
        bool first_modified = dao->insert_or_replace(test_fee_rate);
        CHECK(first_modified == true);

        // Modify the fee rate
        test_fee_rate.takerFeeRate = currency<uint64_t>("0.0007");
        test_fee_rate.makerFeeRate = currency<uint64_t>("0.0002");

        // Insert/replace with modified data
        bool second_modified = dao->insert_or_replace(test_fee_rate);
        CHECK(second_modified == true);

        // Verify still only one record but with updated values
        auto count = dao->count();
        CHECK(count == 1);

        auto eth_condition = QueryCondition::where("symbol", QueryOperator::Equal);
        auto records = dao->query(eth_condition, "ETHUSDT");
        REQUIRE(records.size() == 1);
        CHECK(records[0].takerFeeRate.to_string() == "0.0007");
        CHECK(records[0].makerFeeRate.to_string() == "0.0002");
    }

    SECTION("Multiple identical inserts - only first should modify") {
        bool modified1 = dao->insert_or_replace(test_fee_rate);
        CHECK(modified1 == true);

        bool modified2 = dao->insert_or_replace(test_fee_rate);
        CHECK(modified2 == false);

        bool modified3 = dao->insert_or_replace(test_fee_rate);
        CHECK(modified3 == false);

        // Verify still only one record
        auto count = dao->count();
        CHECK(count == 1);
    }
}

TEST_CASE("Direct operations", "[dao][fee_rate][operations][DATAHUB-017]") {
    TestDatabase test_db;

    // Create DAO with symbol as primary key - table created automatically in constructor
    auto dao = data_model<FeeRate, &FeeRate::symbol>::create(test_db.db, test_db.strand());

    // Create test data
    FeeRate test_fee_rate{
        std::string{"ETHUSDT"},   // symbol (primary key)
        std::nullopt,             // baseCoin
        currency<uint64_t>("0.0006"),  // takerFeeRate
        currency<uint64_t>("0.0001")   // makerFeeRate
    };

    SECTION("Direct Insert operation") {
        Insert insert_op(dao);
        insert_op(test_fee_rate);

        // Verify insertion
        auto all_records = dao->query();
        CHECK(all_records.size() == 1);
    }

    SECTION("Direct InsertOrReplace operation") {
        InsertOrReplace insert_or_replace_op(dao);

        // First insert - should return true
        bool first_modified = insert_or_replace_op(test_fee_rate);
        CHECK(first_modified == true);

        // Second insert with same data - should return false
        bool second_modified = insert_or_replace_op(test_fee_rate);
        CHECK(second_modified == false);

        // Verify only one record
        auto all_records = dao->query();
        CHECK(all_records.size() == 1);
    }
    
    SECTION("Direct Query operation") {
        // Insert test data first
        dao->insert(test_fee_rate);
        
        // Test query all
        Query query_all_op(dao);
        auto all_records = query_all_op();
        CHECK(all_records.size() == 1);
        
        // Test query with condition
        auto condition = QueryCondition::where("symbol", QueryOperator::Equal);
        Query query_cond_op(dao, condition);
        auto filtered_records = query_cond_op("ETHUSDT");
        CHECK(filtered_records.size() == 1);
        CHECK(filtered_records[0].symbol == "ETHUSDT");
    }
    
    SECTION("Direct Update operation") {
        // Insert test data first
        dao->insert(test_fee_rate);
        
        // Modify the fee rate
        test_fee_rate.takerFeeRate = currency<uint64_t>("0.0007");
        
        // Update using operation
        Update update_op(dao);
        update_op(test_fee_rate);
        
        // Verify update
        auto all_records = dao->query();
        CHECK(all_records.size() == 1);
        CHECK(all_records[0].takerFeeRate.to_string() == "0.0007");
    }
    
    SECTION("Direct Delete operation") {
        // Insert test data first
        dao->insert(test_fee_rate);
        
        // Delete using operation
        auto condition = QueryCondition::where("symbol", QueryOperator::Equal);
        Delete delete_op(dao, condition);
        delete_op("ETHUSDT");
        
        // Verify deletion
        auto all_records = dao->query();
        CHECK(all_records.empty());
    }
    
    SECTION("Direct Count operation") {
        // Insert test data first
        dao->insert(test_fee_rate);
        
        // Count all
        Count count_all_op(dao);
        auto total_count = count_all_op();
        CHECK(total_count == 1);
        
        // Count with condition
        auto condition = QueryCondition::where("symbol", QueryOperator::Equal);
        Count count_cond_op(dao, condition);
        auto filtered_count = count_cond_op("ETHUSDT");
        CHECK(filtered_count == 1);
    }
}

namespace SQLite {
void assertion_failed(const char* apFile, int apLine, const char* apFunc, const char* apExpr, const char* apMsg) {
    std::cerr << "SQLite assertion failed: " << apFile << ":" << apLine << " in " << apFunc << "() - " << apExpr;
    if (apMsg) std::cerr << " (" << apMsg << ")";
    std::cerr << std::endl;
    std::abort(); // Or throw exception if you prefer
}
}
