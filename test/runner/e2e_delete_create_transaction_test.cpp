#include "test/test_utility/include/test_helper.h"

using namespace graphflow::testing;

class BaseDeleteCreateTrxTest : public DBTest {
public:
    void SetUp() override {
        DBTest::SetUp();
        readConn = make_unique<Connection>(database.get());
    }

public:
    unique_ptr<Connection> readConn;
};

class CreateDeleteNodeTrxTest : public BaseDeleteCreateTrxTest {
public:
    string getInputCSVDir() override { return "dataset/node-insertion-deletion-tests/"; }

    void checkNodeExists(Connection* connection, uint64_t nodeID) {
        auto result =
            connection->query("MATCH (a:person) WHERE a.ID=" + to_string(nodeID) + " RETURN a.ID");
        ASSERT_EQ(result->getNext()->getResultValue(0)->getInt64Val(), nodeID);
    }

    void checkNodeNotExists(Connection* connection, uint64_t nodeID) {
        auto result =
            connection->query("MATCH (a:person) WHERE a.ID=" + to_string(nodeID) + " RETURN a.ID");
        ASSERT_FALSE(result->hasNext());
    }

    void deleteNodes(Connection* connection, vector<uint64_t> nodeIDs) {
        auto predicate = "WHERE a.ID=" + to_string(nodeIDs[0]);
        for (auto i = 1u; i < nodeIDs.size(); ++i) {
            predicate += " OR a.ID=" + to_string(nodeIDs[i]);
        }
        auto query = "MATCH (a:person) " + predicate + " DELETE a";
        auto result = connection->query(query);
        ASSERT_TRUE(result->isSuccess());
    }

    int64_t getCountStarVal(Connection* connection, const string& query) {
        auto result = connection->query(query);
        return result->getNext()->getResultValue(0)->getInt64Val();
    }

    int64_t getNumNodes(Connection* connection) {
        auto result = connection->query("MATCH (:person) RETURN count(*)");
        return result->getNext()->getResultValue(0)->getInt64Val();
    }

    void addNodes(Connection* connection, uint64_t numNodes) {
        for (auto i = 0u; i < numNodes; ++i) {
            auto id = 10000 + i;
            auto result = conn->query("CREATE (a:person {ID: " + to_string(id) + "})");
            ASSERT_TRUE(result->isSuccess());
        }
    }
};

TEST_F(CreateDeleteNodeTrxTest, ReadBeforeCommit) {
    auto nodeIDsToDelete = vector<uint64_t>{10, 1400, 6000};
    conn->beginWriteTransaction();
    deleteNodes(conn.get(), nodeIDsToDelete);
    for (auto& nodeIDToDelete : nodeIDsToDelete) {
        checkNodeExists(readConn.get(), nodeIDToDelete);
        checkNodeNotExists(conn.get(), nodeIDToDelete);
    }
}

TEST_F(CreateDeleteNodeTrxTest, ReadAfterCommitNormalExecution) {
    auto nodeIDsToDelete = vector<uint64_t>{10, 1400, 6000};
    conn->beginWriteTransaction();
    deleteNodes(conn.get(), nodeIDsToDelete);
    conn->commit();
    for (auto& nodeIDToDelete : nodeIDsToDelete) {
        checkNodeNotExists(readConn.get(), nodeIDToDelete);
        checkNodeNotExists(conn.get(), nodeIDToDelete);
    }
}

TEST_F(CreateDeleteNodeTrxTest, ReadAfterRollbackNormalExecution) {
    auto nodeIDsToDelete = vector<uint64_t>{10, 1400, 6000};
    conn->beginWriteTransaction();
    deleteNodes(conn.get(), nodeIDsToDelete);
    conn->rollback();
    for (auto& nodeIDToDelete : nodeIDsToDelete) {
        checkNodeExists(readConn.get(), nodeIDToDelete);
        checkNodeExists(conn.get(), nodeIDToDelete);
    }
}

TEST_F(CreateDeleteNodeTrxTest, DeleteEntireMorselCommitNormalExecution) {
    conn->beginWriteTransaction();
    conn->query("MATCH (a:person) WHERE a.ID < 4096 DELETE a");
    auto query = "MATCH (a:person) WHERE a.ID < 4096 RETURN count(*)";
    ASSERT_EQ(getCountStarVal(conn.get(), query), 0);
    ASSERT_EQ(getCountStarVal(readConn.get(), query), 4096);
    conn->commit();
    ASSERT_EQ(getCountStarVal(conn.get(), query), 0);
    ASSERT_EQ(getCountStarVal(readConn.get(), query), 0);
}

TEST_F(CreateDeleteNodeTrxTest, DeleteAllNodesCommitNormalExecution) {
    conn->beginWriteTransaction();
    conn->query("MATCH (a:person) DELETE a");
    auto query = "MATCH (a:person) RETURN count(DISTINCT a.ID)";
    ASSERT_EQ(getCountStarVal(conn.get(), query), 0);
    ASSERT_EQ(getCountStarVal(readConn.get(), query), 10000);
    conn->commit();
    ASSERT_EQ(getCountStarVal(conn.get(), query), 0);
    ASSERT_EQ(getCountStarVal(readConn.get(), query), 0);
}

TEST_F(CreateDeleteNodeTrxTest, DeleteAllNodesCommitRecovery) {
    conn->beginWriteTransaction();
    conn->query("MATCH (a:person) DELETE a");
    conn->commitButSkipCheckpointingForTestingRecovery();
    // This should run the recovery algorithm
    createDBAndConn();
    auto query = "MATCH (a:person) RETURN count(DISTINCT a.ID)";
    ASSERT_EQ(getCountStarVal(conn.get(), query), 0);
}

TEST_F(CreateDeleteNodeTrxTest, DeleteAllNodesRollbackNormalExecution) {
    conn->beginWriteTransaction();
    conn->query("MATCH (a:person) DELETE a");
    conn->rollback();
    auto query = "MATCH (a:person) RETURN count(DISTINCT a.ID)";
    ASSERT_EQ(getCountStarVal(conn.get(), query), 10000);
    ASSERT_EQ(getCountStarVal(readConn.get(), query), 10000);
}

TEST_F(CreateDeleteNodeTrxTest, DeleteAllNodesRollbackRecovery) {
    conn->beginWriteTransaction();
    conn->query("MATCH (a:person) DELETE a");
    conn->rollbackButSkipCheckpointingForTestingRecovery();
    // This should run the recovery algorithm
    createDBAndConn();
    auto query = "MATCH (a:person) RETURN count(DISTINCT a.ID)";
    ASSERT_EQ(getCountStarVal(conn.get(), query), 10000);
}

// TODO(Guodong): We need to extend these tests with queries that verify that the IDs are deleted,
// added, and can be queried correctly.
TEST_F(CreateDeleteNodeTrxTest, SimpleAddCommitInTrx) {
    conn->beginWriteTransaction();
    addNodes(conn.get(), 1000);
    ASSERT_EQ(getNumNodes(conn.get()), 11000);
    ASSERT_EQ(getNumNodes(readConn.get()), 10000);
}

TEST_F(CreateDeleteNodeTrxTest, SimpleAddCommitNormalExecution) {
    conn->beginWriteTransaction();
    addNodes(conn.get(), 1000);
    conn->commit();
    ASSERT_EQ(getNumNodes(conn.get()), 11000);
}

TEST_F(CreateDeleteNodeTrxTest, SimpleAddCommitRecovery) {
    conn->beginWriteTransaction();
    addNodes(conn.get(), 1000);
    conn->commitButSkipCheckpointingForTestingRecovery();
    createDBAndConn(); // run recovery
    ASSERT_EQ(getNumNodes(conn.get()), 11000);
}

TEST_F(CreateDeleteNodeTrxTest, SimpleAddRollbackNormalExecution) {
    conn->beginWriteTransaction();
    addNodes(conn.get(), 1000);
    conn->rollback();
    ASSERT_EQ(getNumNodes(conn.get()), 10000);
}

TEST_F(CreateDeleteNodeTrxTest, SimpleAddRollbackRecovery) {
    conn->beginWriteTransaction();
    addNodes(conn.get(), 1000);
    conn->rollbackButSkipCheckpointingForTestingRecovery();
    createDBAndConn(); // run recovery
    ASSERT_EQ(getNumNodes(conn.get()), 10000);
}

class CreateDeleteRelTrxTest : public BaseDeleteCreateTrxTest {
public:
    string getInputCSVDir() override { return "dataset/rel-insertion-tests/"; }

    void insertKnowsRels(const string& srcLabel, const string& dstLabel, uint64_t numRelsToInsert,
        bool testNullAndLongString = false) {
        for (auto i = 0u; i < numRelsToInsert; ++i) {
            auto length = to_string(i);
            auto place = to_string(i);
            auto tag = to_string(i);
            if (testNullAndLongString) {
                length = "null";
                place = getLongStr(place);
            }
            insertKnowsRel(
                srcLabel, dstLabel, "1", to_string(i + 1), length, place, "['" + tag + "']");
        }
    }

    void insertKnowsRel(const string& srcLabel, const string& dstLabel, const string& srcID,
        const string& dstID, const string& length, const string& place, const string& tag) {
        auto matchClause = "MATCH (a:" + srcLabel + "),(b:" + dstLabel + ") WHERE a.ID=" + srcID +
                           " AND b.ID=" + dstID + " ";
        auto createClause = "CREATE (a)-[e:knows {length:" + length + ",place:'" + place +
                            "',tag:" + tag + "}]->(b)";
        assert(conn->query(matchClause + createClause));
    }

    vector<string> readAllKnowsProperty(Connection* connection, const string& srcLabel,
        const string& dstLabel, bool validateBwdOrder = false) {
        auto result = connection->query("MATCH (a:" + srcLabel + ")-[e:knows]->(b:" + dstLabel +
                                        ") RETURN e.length, e.place, e.tag");
        return TestHelper::convertResultToString(*result, true /* checkOrder, do not sort */);
    }

    static string getLongStr(const string& val) { return "long long string prefix " + val; }

public:
    static const int64_t smallNumRelsToInsert = 10;
    // For a relTable with simple relation(src,dst nodeIDs are 1): we need to insert at least
    // PAGE_SIZE / 8 number of rels to make a smallList become largeList.
    // For a relTable with complex relation(src,dst nodeIDs > 1): we need to insert at least
    // PAGE_SIZE / 16 number of rels to make a smallList become largeList.
    static const int64_t largeNumRelsToInsert = 510;
};

static void validateInsertedRel(
    const string& result, uint64_t param, bool testNullAndLongString = false) {
    auto length = to_string(param);
    auto place = to_string(param);
    if (testNullAndLongString) {
        length = ""; // null
        place = CreateDeleteRelTrxTest::getLongStr(place);
    }
    auto groundTruth = length + "|" + place + "|[" + to_string(param) + "]";
    ASSERT_STREQ(groundTruth.c_str(), result.c_str());
}

static void validateInsertRelsToEmptyListSucceed(
    vector<string> resultStr, uint64_t numRelsInserted, bool testNullAndLongString = false) {
    for (auto i = 0u; i < numRelsInserted; i++) {
        validateInsertedRel(resultStr[i], i, testNullAndLongString);
    }
}

static void validateInsertRelsToEmptyListFail(vector<string> resultStr) {
    ASSERT_EQ(resultStr.size(), 0);
}

TEST_F(CreateDeleteRelTrxTest, InsertRelsToEmptyListCommitNormalExecution) {
    conn->beginWriteTransaction();
    insertKnowsRels("animal", "animal", smallNumRelsToInsert);
    validateInsertRelsToEmptyListSucceed(
        readAllKnowsProperty(conn.get(), "animal", "animal"), smallNumRelsToInsert);
    validateInsertRelsToEmptyListFail(readAllKnowsProperty(readConn.get(), "animal", "animal"));
    conn->commit();
    validateInsertRelsToEmptyListSucceed(
        readAllKnowsProperty(conn.get(), "animal", "animal"), smallNumRelsToInsert);
}

TEST_F(CreateDeleteRelTrxTest, InsertRelsToEmptyListCommitRecovery) {
    conn->beginWriteTransaction();
    insertKnowsRels("animal", "animal", smallNumRelsToInsert);
    conn->commitButSkipCheckpointingForTestingRecovery();
    createDBAndConn(); // run recovery
    validateInsertRelsToEmptyListSucceed(
        readAllKnowsProperty(conn.get(), "animal", "animal"), smallNumRelsToInsert);
}

TEST_F(CreateDeleteRelTrxTest, InsertRelsToEmptyListRollbackNormalExecution) {
    conn->beginWriteTransaction();
    insertKnowsRels("animal", "animal", smallNumRelsToInsert);
    conn->rollback();
    validateInsertRelsToEmptyListFail(readAllKnowsProperty(conn.get(), "animal", "animal"));
}

TEST_F(CreateDeleteRelTrxTest, InsertRelsToEmptyListRollbackRecovery) {
    conn->beginWriteTransaction();
    insertKnowsRels("animal", "animal", smallNumRelsToInsert);
    conn->rollbackButSkipCheckpointingForTestingRecovery();
    createDBAndConn(); // run recovery
    validateInsertRelsToEmptyListFail(readAllKnowsProperty(conn.get(), "animal", "animal"));
}

TEST_F(CreateDeleteRelTrxTest, InsertManyRelsToEmptyListWithNullAndLongStrCommitNormalExecution) {
    conn->beginWriteTransaction();
    insertKnowsRels("animal", "animal", largeNumRelsToInsert, true /* testNullAndLongString */);
    validateInsertRelsToEmptyListSucceed(readAllKnowsProperty(conn.get(), "animal", "animal"),
        largeNumRelsToInsert, true /* testNullAndLongString */);
    validateInsertRelsToEmptyListFail(readAllKnowsProperty(readConn.get(), "animal", "animal"));
    conn->commit();
    validateInsertRelsToEmptyListSucceed(readAllKnowsProperty(conn.get(), "animal", "animal"),
        largeNumRelsToInsert, true /* testNullAndLongString */);
}

TEST_F(CreateDeleteRelTrxTest, InsertManyRelsToEmptyListWithNullAndLongStrRollbackNormalExecution) {
    conn->beginWriteTransaction();
    insertKnowsRels("animal", "animal", largeNumRelsToInsert, true /* testNullAndLongString */);
    conn->rollback();
    validateInsertRelsToEmptyListFail(readAllKnowsProperty(conn.get(), "animal", "animal"));
}

TEST_F(CreateDeleteRelTrxTest, InsertManyRelsToEmptyListWithNullAndLongStrRollbackRecovery) {
    conn->beginWriteTransaction();
    insertKnowsRels("animal", "animal", largeNumRelsToInsert, true /* testNullAndLongString */);
    conn->rollbackButSkipCheckpointingForTestingRecovery();
    createDBAndConn(); // run recovery
    validateInsertRelsToEmptyListFail(readAllKnowsProperty(conn.get(), "animal", "animal"));
}

static int64_t numAnimalPersonRelsInDB = 51;

static string generateStrFromInt(uint64_t val) {
    string result = to_string(val);
    if (val % 2 == 0) {
        for (auto i = 0; i < 2; i++) {
            result.append(to_string(val));
        }
    }
    return result;
}

static void validateExistingAnimalPersonRel(const string& result, uint64_t param) {
    auto strVal = generateStrFromInt(1000 - param);
    auto groundTruth = to_string(param) + "|" + strVal + "|[" + strVal + "]";
    ASSERT_STREQ(groundTruth.c_str(), result.c_str());
}

static void validateInsertRelsToSmallListSucceed(
    vector<string> resultStr, uint64_t numRelsInserted) {
    auto currIdx = 0u;
    auto insertedRelsOffset = 2;
    for (auto i = 0u; i < insertedRelsOffset; ++i, ++currIdx) {
        validateExistingAnimalPersonRel(resultStr[currIdx], i);
    }
    for (auto i = 0u; i < numRelsInserted; ++i, ++currIdx) {
        validateInsertedRel(resultStr[currIdx], i);
    }
    for (auto i = 0u; i < numAnimalPersonRelsInDB - insertedRelsOffset; ++i, ++currIdx) {
        validateExistingAnimalPersonRel(resultStr[currIdx], i + insertedRelsOffset);
    }
}

static void validateInsertRelsToSmallListFail(vector<string> resultStr) {
    ASSERT_EQ(resultStr.size(), numAnimalPersonRelsInDB);
    validateInsertRelsToSmallListSucceed(resultStr, 0);
}

TEST_F(CreateDeleteRelTrxTest, InsertRelsToSmallListCommitNormalExecution) {
    conn->beginWriteTransaction();
    insertKnowsRels("animal", "person", smallNumRelsToInsert);
    validateInsertRelsToSmallListSucceed(
        readAllKnowsProperty(conn.get(), "animal", "person"), smallNumRelsToInsert);
    validateInsertRelsToSmallListFail(readAllKnowsProperty(readConn.get(), "animal", "person"));
    conn->commit();
    validateInsertRelsToSmallListSucceed(
        readAllKnowsProperty(conn.get(), "animal", "person"), smallNumRelsToInsert);
}

TEST_F(CreateDeleteRelTrxTest, InsertRelsToSmallListCommitRecovery) {
    conn->beginWriteTransaction();
    insertKnowsRels("animal", "person", smallNumRelsToInsert);
    conn->commitButSkipCheckpointingForTestingRecovery();
    createDBAndConn(); // run recovery
    validateInsertRelsToSmallListSucceed(
        readAllKnowsProperty(conn.get(), "animal", "person"), smallNumRelsToInsert);
}

TEST_F(CreateDeleteRelTrxTest, InsertRelsToSmallListRollbackNormalExecution) {
    conn->beginWriteTransaction();
    insertKnowsRels("animal", "person", smallNumRelsToInsert);
    conn->rollback();
    validateInsertRelsToSmallListFail(readAllKnowsProperty(conn.get(), "animal", "person"));
}

TEST_F(CreateDeleteRelTrxTest, InsertRelsToSmallListRollbackRecovery) {
    conn->beginWriteTransaction();
    insertKnowsRels("animal", "person", smallNumRelsToInsert);
    conn->rollbackButSkipCheckpointingForTestingRecovery();
    createDBAndConn(); // run recovery
    validateInsertRelsToSmallListFail(readAllKnowsProperty(conn.get(), "animal", "person"));
}

TEST_F(CreateDeleteRelTrxTest, InsertLargeNumRelsToSmallListCommitNormalExecution) {
    conn->beginWriteTransaction();
    insertKnowsRels("animal", "person", largeNumRelsToInsert);
    validateInsertRelsToSmallListSucceed(
        readAllKnowsProperty(conn.get(), "animal", "person"), largeNumRelsToInsert);
    validateInsertRelsToSmallListFail(readAllKnowsProperty(readConn.get(), "animal", "person"));
    conn->commit();
    validateInsertRelsToSmallListSucceed(
        readAllKnowsProperty(conn.get(), "animal", "person"), largeNumRelsToInsert);
}

TEST_F(CreateDeleteRelTrxTest, InsertLargeNumRelsToSmallListCommitRecovery) {
    conn->beginWriteTransaction();
    insertKnowsRels("animal", "person", largeNumRelsToInsert);
    conn->commitButSkipCheckpointingForTestingRecovery();
    createDBAndConn(); // run recovery
    validateInsertRelsToSmallListSucceed(
        readAllKnowsProperty(conn.get(), "animal", "person"), largeNumRelsToInsert);
}

static int numPersonPersonRelsInDB = 2500;

static void validateExistingPersonPersonRel(const string& result, uint64_t param) {
    auto strVal = generateStrFromInt(3000 - param);
    auto groundTruth = to_string(3 * param) + "|" + strVal + "|[" + strVal + "]";
    ASSERT_STREQ(groundTruth.c_str(), result.c_str());
}

static void validateInsertRelsToLargeListSucceed(
    vector<string> resultStr, uint64_t numRelsInserted, bool testNullAndLongString = false) {
    auto currIdx = 0u;
    for (auto i = 0u; i < numPersonPersonRelsInDB; ++i, ++currIdx) {
        validateExistingPersonPersonRel(resultStr[currIdx], i);
    }
    for (auto i = 0u; i < numRelsInserted; ++i, ++currIdx) {
        validateInsertedRel(resultStr[currIdx], i, testNullAndLongString);
    }
}

static void validateInsertRelsToLargeListFail(vector<string> resultStr) {
    ASSERT_EQ(resultStr.size(), numPersonPersonRelsInDB);
    validateInsertRelsToLargeListSucceed(resultStr, 0);
}

TEST_F(CreateDeleteRelTrxTest, InsertRelsToLargeListCommitNormalExecution) {
    conn->beginWriteTransaction();
    insertKnowsRels("person", "person", smallNumRelsToInsert);
    validateInsertRelsToLargeListSucceed(
        readAllKnowsProperty(conn.get(), "person", "person"), smallNumRelsToInsert);
    validateInsertRelsToLargeListFail(readAllKnowsProperty(readConn.get(), "person", "person"));
    conn->commit();
    validateInsertRelsToLargeListSucceed(
        readAllKnowsProperty(conn.get(), "person", "person"), smallNumRelsToInsert);
}

TEST_F(CreateDeleteRelTrxTest, InsertRelsToLargeListCommitRecovery) {
    conn->beginWriteTransaction();
    insertKnowsRels("person", "person", smallNumRelsToInsert);
    conn->commitButSkipCheckpointingForTestingRecovery();
    createDBAndConn(); // run recovery
    validateInsertRelsToLargeListSucceed(
        readAllKnowsProperty(conn.get(), "person", "person"), smallNumRelsToInsert);
}

TEST_F(CreateDeleteRelTrxTest, InsertRelsToLargeListRollbackNormalExecution) {
    conn->beginWriteTransaction();
    insertKnowsRels("person", "person", smallNumRelsToInsert);
    conn->rollback();
    validateInsertRelsToLargeListFail(readAllKnowsProperty(conn.get(), "person", "person"));
}

TEST_F(CreateDeleteRelTrxTest, InsertRelsToLargeListRollbackRecovery) {
    conn->beginWriteTransaction();
    insertKnowsRels("person", "person", smallNumRelsToInsert);
    conn->rollbackButSkipCheckpointingForTestingRecovery();
    createDBAndConn(); // run recovery
    validateInsertRelsToLargeListFail(readAllKnowsProperty(conn.get(), "person", "person"));
}

TEST_F(CreateDeleteRelTrxTest, InsertRelsToLargeListWithNullCommitNormalExecution) {
    conn->beginWriteTransaction();
    insertKnowsRels("person", "person", smallNumRelsToInsert, true /* testNullAndLongString */);
    validateInsertRelsToLargeListSucceed(readAllKnowsProperty(conn.get(), "person", "person"),
        smallNumRelsToInsert, true /* testNullAndLongString */);
    validateInsertRelsToLargeListFail(readAllKnowsProperty(readConn.get(), "person", "person"));
    conn->commit();
    validateInsertRelsToLargeListSucceed(readAllKnowsProperty(conn.get(), "person", "person"),
        smallNumRelsToInsert, true /* testNullAndLongString */);
}

TEST_F(CreateDeleteRelTrxTest, InsertRelsToLargeListWithNullCommitRecovery) {
    conn->beginWriteTransaction();
    insertKnowsRels("person", "person", smallNumRelsToInsert, true /* testNullAndLongString */);
    conn->commitButSkipCheckpointingForTestingRecovery();
    createDBAndConn(); // run recovery
    validateInsertRelsToLargeListSucceed(readAllKnowsProperty(conn.get(), "person", "person"),
        smallNumRelsToInsert, true /* testNullAndLongString */);
}
