#pragma once

#include <map>

#include "storage/stats/table_statistics.h"
#include "storage/storage_utils.h"

namespace kuzu {
namespace storage {

class RelsStatistics;
class RelTableStats : public TableStatistics {
    friend class RelsStatistics;

public:
    RelTableStats(const catalog::TableSchema& tableSchema)
        : TableStatistics{tableSchema}, nextRelOffset{0} {}
    RelTableStats(uint64_t numRels, common::table_id_t tableID, common::offset_t nextRelOffset)
        : TableStatistics{common::TableType::REL, numRels, tableID, {}}, nextRelOffset{
                                                                             nextRelOffset} {}
    RelTableStats(uint64_t numRels, common::table_id_t tableID,
        std::unordered_map<common::property_id_t, std::unique_ptr<PropertyStatistics>>&&
            propertyStats,
        common::offset_t nextRelOffset)
        : TableStatistics{common::TableType::REL, numRels, tableID, std::move(propertyStats)},
          nextRelOffset{nextRelOffset} {}

    inline common::offset_t getNextRelOffset() const { return nextRelOffset; }

    void serializeInternal(common::FileInfo* fileInfo, uint64_t& offset) final;
    static std::unique_ptr<RelTableStats> deserialize(
        uint64_t numRels, common::table_id_t tableID, common::FileInfo* fileInfo, uint64_t& offset);

private:
    common::offset_t nextRelOffset;
};

// Manages the disk image of the numRels and numRelsPerDirectionBoundTable.
class RelsStatistics : public TablesStatistics {

public:
    // Should only be used by saveInitialRelsStatisticsToFile to start a database from an empty
    // directory.
    RelsStatistics() : TablesStatistics{} {};
    // Should be used when an already loaded database is started from a directory.
    explicit RelsStatistics(const std::string& directory) : TablesStatistics{} {
        readFromFile(directory);
    }

    // Should only be used by tests.
    explicit RelsStatistics(std::unordered_map<common::table_id_t, std::unique_ptr<RelTableStats>>
            relStatisticPerTable_);

    static inline void saveInitialRelsStatisticsToFile(const std::string& directory) {
        std::make_unique<RelsStatistics>()->saveToFile(
            directory, common::DBFileType::ORIGINAL, transaction::TransactionType::READ_ONLY);
    }

    inline RelTableStats* getRelStatistics(common::table_id_t tableID) const {
        auto& tableStatisticPerTable =
            tablesStatisticsContentForReadOnlyTrx->tableStatisticPerTable;
        return (RelTableStats*)tableStatisticPerTable[tableID].get();
    }

    void setNumTuplesForTable(common::table_id_t relTableID, uint64_t numRels) override;

    void updateNumRelsByValue(common::table_id_t relTableID, int64_t value);

    common::offset_t getNextRelOffset(
        transaction::Transaction* transaction, common::table_id_t tableID);

protected:
    inline std::unique_ptr<TableStatistics> constructTableStatistic(
        catalog::TableSchema* tableSchema) override {
        return std::make_unique<RelTableStats>(*tableSchema);
    }

    inline std::unique_ptr<TableStatistics> constructTableStatistic(
        TableStatistics* tableStatistics) override {
        return std::make_unique<RelTableStats>(*(RelTableStats*)tableStatistics);
    }

    inline std::string getTableStatisticsFilePath(
        const std::string& directory, common::DBFileType dbFileType) override {
        return StorageUtils::getRelsStatisticsFilePath(directory, dbFileType);
    }

    inline void increaseNextRelOffset(common::table_id_t relTableID, uint64_t numTuples) {
        ((RelTableStats*)tablesStatisticsContentForWriteTrx->tableStatisticPerTable.at(relTableID)
                .get())
            ->nextRelOffset += numTuples;
    }
};

} // namespace storage
} // namespace kuzu
