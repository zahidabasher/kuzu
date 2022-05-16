#pragma once

#include "src/common/types/include/types_include.h"
#include "src/storage/include/buffer_manager.h"
#include "src/storage/include/file_handle.h"
#include "src/storage/include/wal/wal_record.h"

namespace spdlog {
class logger;
}

namespace graphflow {
namespace storage {

using lock_t = unique_lock<mutex>;
constexpr uint64_t WAL_HEADER_PAGE_SIZE = DEFAULT_PAGE_SIZE;
constexpr uint64_t WAL_HEADER_PAGE_NUM_RECORDS_FIELD_SIZE = sizeof(uint64_t);
constexpr uint64_t WAL_HEADER_PAGE_NEXT_HEADER_PAGE_IDX_FIELD_SIZE = sizeof(uint64_t);
constexpr uint64_t WAL_HEADER_PAGE_PREFIX_FIELD_SIZES =
    WAL_HEADER_PAGE_NUM_RECORDS_FIELD_SIZE + WAL_HEADER_PAGE_NEXT_HEADER_PAGE_IDX_FIELD_SIZE;

class WALIterator;

class BaseWALAndWALIterator {

protected:
    BaseWALAndWALIterator() : BaseWALAndWALIterator(nullptr) {}

    BaseWALAndWALIterator(shared_ptr<FileHandle> fileHandle)
        : fileHandle(fileHandle), offsetInCurrentHeaderPage{INT64_MAX}, currentHeaderPageIdx{
                                                                            INT64_MAX} {
        currentHeaderPageBuffer = make_unique<uint8_t[]>(WAL_HEADER_PAGE_SIZE);
    }

protected:
    inline uint64_t getNumRecordsInCurrentHeaderPage() {
        return ((uint64_t*)currentHeaderPageBuffer.get())[0];
    }

    inline void incrementNumRecordsInCurrentHeaderPage() {
        ((uint64_t*)currentHeaderPageBuffer.get())[0]++;
    }

    inline uint64_t getNextHeaderPageOfCurrentHeaderPage() {
        return ((uint64_t*)currentHeaderPageBuffer.get())[1];
    }

    inline void setNextHeaderPageOfCurrentHeaderPage(uint64_t nextHeaderPageIdx) {
        ((uint64_t*)currentHeaderPageBuffer.get())[1] = nextHeaderPageIdx;
    }

    inline void resetCurrentHeaderPagePrefix() {
        ((uint64_t*)currentHeaderPageBuffer.get())[0] = 0;
        setNextHeaderPageOfCurrentHeaderPage(UINT64_MAX); // set next page idx to UINT64_MAX as null
        offsetInCurrentHeaderPage = WAL_HEADER_PAGE_PREFIX_FIELD_SIZES;
    }

public:
    shared_ptr<FileHandle> fileHandle;
    // Used by WAL as the next offset to write and by WALIterator as the next offset to read
    uint64_t offsetInCurrentHeaderPage;
    // First header page of the WAL, if it exists, is always located at page 0 of the WAL.
    uint64_t currentHeaderPageIdx;
    unique_ptr<uint8_t[]> currentHeaderPageBuffer;
};

class WAL : public BaseWALAndWALIterator {
    friend WALIterator;

public:
    WAL(const string& path, BufferManager& bufferManager);

    // Destructing WAL flushes any unwritten header page but not the other pages. The caller
    // which possibly has access to the buffer manager needs to ensure any unwritten pages
    // are also flushed to disk.
    ~WAL() {
        lock_t lck{mtx};
        // WAL only keeps track of the current header page. Any prior header pages are already
        // written to disk. So we only flush the current header page.
        flushHeaderPages();
    }

    inline unique_ptr<WALIterator> getIterator() {
        lock_t lck{mtx};
        flushHeaderPages();
        return make_unique<WALIterator>(fileHandle, mtx);
    }

    /**
     * Adds a new log entry for an update to an existing node property page.
     *
     * @param nodeLabel ID of the label of the node (needed to identify the file)
     * @param propertyID ID of node property (needed to identify the file)
     * @param pageIdxInOriginalFile index of the updated page in the original file
     * @return index of the updated page in the wal
     */
    uint32_t logStructuredNodePropertyPageRecord(
        label_t nodeLabel, uint32_t propertyID, uint32_t pageIdxInOriginalFile);
    /**
     * Adds a new log entry for an update to an existing property page of an adj column.
     *
     * @param nodeLabel ID of the label of the src node (needed to identify the file)
     * @param relabel ID of the label of the rel (needed to identify the file)
     * @param propertyID ID of rel property (needed to identify the file)
     * @param pageIdxInOriginalFile index of the updated page in the original file
     * @return index of the updated page in the wal
     */
    uint32_t logStructuredAdjColumnPropertyPage(
        label_t nodeLabel, label_t relLabel, uint32_t propertyID, uint32_t pageIdxInOriginalFile);
    void logCommit(uint64_t transactionID);
    // Removes the contents of WAL file.
    void clearWAL();

    // We might need another way to check that the last record is commit for recovery and then
    // we might remove this for now to reduce our code size.
    inline bool isLastLoggedRecordCommit() {
        lock_t lck{mtx};
        return isLastLoggedRecordCommit_;
    }

    void flushAllPages(BufferManager* bufferManager);

    inline bool isEmptyWAL() {
        return currentHeaderPageIdx == 0 && (getNumRecordsInCurrentHeaderPage() == 0);
    }

private:
    inline void createFileHandle(const string& path) {
        fileHandle = make_shared<FileHandle>(
            path, FileHandle::O_DefaultPagedExistingDBFileCreateIfNotExists);
    }

    inline void flushHeaderPages() {
        if (!isEmptyWAL()) {
            fileHandle->writePage(currentHeaderPageBuffer.get(), currentHeaderPageIdx);
        }
    }

    void initCurrentPageAndSetIsLastRecordCommitToTrueIfNecessary();
    void addNewWALRecordWithoutLock(WALRecord& walRecord);
    void setIsLastRecordCommitToTrueIfNecessary();

private:
    shared_ptr<spdlog::logger> logger;
    mutex mtx;
    BufferManager& bufferManager;
    bool isLastLoggedRecordCommit_;
};

class WALIterator : public BaseWALAndWALIterator {
public:
    explicit WALIterator(shared_ptr<FileHandle> fileHandle, mutex& mtx);

    inline bool hasNextRecord() {
        lock_t lck{mtx};
        return hasNextRecordWithoutLock();
    }

    void getNextRecord(WALRecord& retVal);

private:
    inline bool hasNextRecordWithoutLock() {
        return numRecordsReadInCurrentHeaderPage < getNumRecordsInCurrentHeaderPage();
    }

public:
    mutex& mtx;
    uint64_t numRecordsReadInCurrentHeaderPage;
};

} // namespace storage
} // namespace graphflow
