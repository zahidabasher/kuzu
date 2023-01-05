#include "storage/storage_structure/lists/lists_update_store.h"

#include "storage/storage_structure/lists/lists.h"

namespace kuzu {
namespace storage {

ListsUpdatesForNodeOffset::ListsUpdatesForNodeOffset(const RelTableSchema& relTableSchema)
    : isNewlyAddedNode{false} {
    for (auto& relProperty : relTableSchema.properties) {
        updatedPersistentListOffsets.emplace(
            relProperty.propertyID, UpdatedPersistentListOffsets{});
    }
}

bool ListsUpdatesForNodeOffset::hasUpdates() const {
    for (auto& [propertyID, persistentStoreUpdate] : updatedPersistentListOffsets) {
        if (persistentStoreUpdate.hasUpdates()) {
            return true;
        }
    }
    return isNewlyAddedNode || !insertedRelsTupleIdxInFT.empty() || !deletedRelIDs.empty();
}

ListsUpdatesStore::ListsUpdatesStore(MemoryManager& memoryManager, RelTableSchema& relTableSchema)
    : relTableSchema{relTableSchema}, memoryManager{memoryManager} {
    initInsertedRels();
    initListsUpdatesPerTablePerDirection();
}

bool ListsUpdatesStore::isNewlyAddedNode(ListFileID& listFileID, node_offset_t nodeOffset) const {
    auto listsUpdatesForNodeOffset = getListsUpdatesForNodeOffsetIfExists(listFileID, nodeOffset);
    if (listsUpdatesForNodeOffset == nullptr) {
        return false;
    }
    return listsUpdatesForNodeOffset->isNewlyAddedNode;
}

uint64_t ListsUpdatesStore::getNumDeletedRels(
    ListFileID& listFileID, node_offset_t nodeOffset) const {
    auto listsUpdatesForNodeOffset = getListsUpdatesForNodeOffsetIfExists(listFileID, nodeOffset);
    if (listsUpdatesForNodeOffset == nullptr) {
        return 0;
    }
    return listsUpdatesForNodeOffset->deletedRelIDs.size();
}

bool ListsUpdatesStore::isRelDeletedInPersistentStore(
    ListFileID& listFileID, node_offset_t nodeOffset, int64_t relID) const {
    auto listsUpdatesForNodeOffset = getListsUpdatesForNodeOffsetIfExists(listFileID, nodeOffset);
    if (listsUpdatesForNodeOffset == nullptr) {
        return false;
    }
    return listsUpdatesForNodeOffset->deletedRelIDs.contains(relID);
}

bool ListsUpdatesStore::hasUpdates() const {
    for (auto relDirection : REL_DIRECTIONS) {
        for (auto& [tableID, listsUpdatesPerTable] :
            listsUpdatesPerTablePerDirection[relDirection]) {
            for (auto& [chunkIdx, listsUpdatesPerChunk] : listsUpdatesPerTable) {
                for (auto& [nodeOffset, listsUpdatesForNodeOffset] : listsUpdatesPerChunk) {
                    if (listsUpdatesForNodeOffset->hasUpdates()) {
                        return true;
                    }
                }
            }
        }
    }
    return false;
}

// Note: This function also resets the overflowptr of each string in inMemList if necessary.
void ListsUpdatesStore::readInsertedRelsToList(ListFileID& listFileID,
    vector<ft_tuple_idx_t> tupleIdxes, InMemList& inMemList, uint64_t numElementsInPersistentStore,
    DiskOverflowFile* diskOverflowFile, DataType dataType,
    NodeIDCompressionScheme* nodeIDCompressionScheme) {
    ftOfInsertedRels->copyToInMemList(getColIdxInFT(listFileID), tupleIdxes,
        inMemList.getListData(), inMemList.nullMask.get(), numElementsInPersistentStore,
        diskOverflowFile, dataType, nodeIDCompressionScheme);
}

void ListsUpdatesStore::insertRelIfNecessary(const shared_ptr<ValueVector>& srcNodeIDVector,
    const shared_ptr<ValueVector>& dstNodeIDVector,
    const vector<shared_ptr<ValueVector>>& relPropertyVectors) {
    auto srcNodeID = srcNodeIDVector->getValue<nodeID_t>(
        srcNodeIDVector->state->selVector->selectedPositions[0]);
    auto dstNodeID = dstNodeIDVector->getValue<nodeID_t>(
        dstNodeIDVector->state->selVector->selectedPositions[0]);
    bool hasInsertedToFT = false;
    auto vectorsToAppendToFT = vector<shared_ptr<ValueVector>>{srcNodeIDVector, dstNodeIDVector};
    vectorsToAppendToFT.insert(
        vectorsToAppendToFT.end(), relPropertyVectors.begin(), relPropertyVectors.end());
    for (auto direction : REL_DIRECTIONS) {
        auto boundNodeID = direction == RelDirection::FWD ? srcNodeID : dstNodeID;
        if (listsUpdatesPerTablePerDirection[direction].contains(boundNodeID.tableID)) {
            if (!hasInsertedToFT) {
                ftOfInsertedRels->append(vectorsToAppendToFT);
                hasInsertedToFT = true;
            }
            getOrCreateListsUpdatesForNodeOffset(direction, boundNodeID)
                ->insertedRelsTupleIdxInFT.push_back(ftOfInsertedRels->getNumTuples() - 1);
        }
    }
}

void ListsUpdatesStore::deleteRelIfNecessary(const shared_ptr<ValueVector>& srcNodeIDVector,
    const shared_ptr<ValueVector>& dstNodeIDVector, const shared_ptr<ValueVector>& relIDVector) {
    auto srcNodeID = srcNodeIDVector->getValue<nodeID_t>(
        srcNodeIDVector->state->selVector->selectedPositions[0]);
    auto dstNodeID = dstNodeIDVector->getValue<nodeID_t>(
        dstNodeIDVector->state->selVector->selectedPositions[0]);
    auto relID =
        relIDVector->getValue<int64_t>(relIDVector->state->selVector->selectedPositions[0]);
    auto tupleIdx = getTupleIdxIfInsertedRel(relID);
    if (tupleIdx != -1) {
        // If the rel that we are going to delete is a newly inserted rel, we need to delete
        // its tupleIdx from the insertedRelsTupleIdxInFT of listsUpdatesStore in FWD and BWD
        // direction. Note: we don't reuse the space for inserted rel tuple in factorizedTable.
        for (auto direction : REL_DIRECTIONS) {
            auto boundNodeID = direction == RelDirection::FWD ? srcNodeID : dstNodeID;
            if (listsUpdatesPerTablePerDirection[direction].contains(boundNodeID.tableID)) {
                auto& insertedRelsTupleIdxInFT =
                    listsUpdatesPerTablePerDirection[direction]
                        .at(boundNodeID.tableID)[StorageUtils::getListChunkIdx(boundNodeID.offset)]
                        .at(boundNodeID.offset)
                        ->insertedRelsTupleIdxInFT;
                assert(find(insertedRelsTupleIdxInFT.begin(), insertedRelsTupleIdxInFT.end(),
                           tupleIdx) != insertedRelsTupleIdxInFT.end());
                insertedRelsTupleIdxInFT.erase(remove(insertedRelsTupleIdxInFT.begin(),
                                                   insertedRelsTupleIdxInFT.end(), tupleIdx),
                    insertedRelsTupleIdxInFT.end());
            }
        }
    } else {
        for (auto direction : REL_DIRECTIONS) {
            auto boundNodeID = direction == RelDirection::FWD ? srcNodeID : dstNodeID;
            if (listsUpdatesPerTablePerDirection[direction].contains(boundNodeID.tableID)) {
                getOrCreateListsUpdatesForNodeOffset(direction, boundNodeID)
                    ->deletedRelIDs.insert(relID);
            }
        }
    }
}

uint64_t ListsUpdatesStore::getNumInsertedRelsForNodeOffset(
    ListFileID& listFileID, node_offset_t nodeOffset) const {
    auto listsUpdatesForNodeOffset = getListsUpdatesForNodeOffsetIfExists(listFileID, nodeOffset);
    if (listsUpdatesForNodeOffset == nullptr) {
        return 0;
    }
    return listsUpdatesForNodeOffset->insertedRelsTupleIdxInFT.size();
}

void ListsUpdatesStore::readValues(
    ListFileID& listFileID, ListHandle& listHandle, shared_ptr<ValueVector> valueVector) const {
    auto numTuplesToRead = listHandle.getNumValuesToRead();
    auto nodeOffset = listHandle.getBoundNodeOffset();
    if (numTuplesToRead == 0) {
        valueVector->state->initOriginalAndSelectedSize(0);
        return;
    }
    auto vectorsToRead = vector<shared_ptr<ValueVector>>{valueVector};
    auto columnsToRead = vector<ft_col_idx_t>{getColIdxInFT(listFileID)};
    auto relNodeTableAndDir = getRelNodeTableAndDirFromListFileID(listFileID);
    auto& listUpdates = listsUpdatesPerTablePerDirection[relNodeTableAndDir.dir]
                            .at(relNodeTableAndDir.srcNodeTableID)
                            .at(StorageUtils::getListChunkIdx(nodeOffset))
                            .at(nodeOffset);
    ftOfInsertedRels->lookup(vectorsToRead, columnsToRead, listUpdates->insertedRelsTupleIdxInFT,
        listHandle.getStartElemOffset(), numTuplesToRead);
    valueVector->state->originalSize = numTuplesToRead;
}

bool ListsUpdatesStore::hasAnyDeletedRelsInPersistentStore(
    ListFileID& listFileID, node_offset_t nodeOffset) const {
    auto listsUpdatesForNodeOffset = getListsUpdatesForNodeOffsetIfExists(listFileID, nodeOffset);
    if (listsUpdatesForNodeOffset == nullptr) {
        return false;
    }
    return !listsUpdatesForNodeOffset->deletedRelIDs.empty();
}

void ListsUpdatesStore::updateRelIfNecessary(const shared_ptr<ValueVector>& srcNodeIDVector,
    const shared_ptr<ValueVector>& dstNodeIDVector, const ListsUpdateInfo& listsUpdateInfo) {
    auto srcNodeID = srcNodeIDVector->getValue<nodeID_t>(
        srcNodeIDVector->state->selVector->selectedPositions[0]);
    auto dstNodeID = dstNodeIDVector->getValue<nodeID_t>(
        dstNodeIDVector->state->selVector->selectedPositions[0]);
    bool insertUpdatedRel = true;
    for (auto direction : REL_DIRECTIONS) {
        auto boundNodeID = direction == FWD ? srcNodeID : dstNodeID;
        // We should only store the property update if the property is stored as a list in the
        // current direction. (E.g. We update a rel property of a MANY-ONE rel table which stores
        // the property as column in the fwd direction, and list in the bwd direction, we should
        // only handle the updates for the bwd direction in this function).
        if (listsUpdatesPerTablePerDirection[direction].contains(boundNodeID.tableID)) {
            // The rel that we are going to update either stored in persistentStore or updateStore.
            if (listsUpdateInfo.isStoredInPersistentStore()) {
                // If the rel is stored in persistentStore, we should store the update in the
                // listsUpdates, and store the <list_offset_t, ft_tuple_idx> index pair inside the
                // updatedPersistentListOffsets of the boundNode.
                if (insertUpdatedRel) {
                    listsUpdates.at(listsUpdateInfo.propertyID)
                        ->append(vector<shared_ptr<ValueVector>>{listsUpdateInfo.propertyVector});
                    insertUpdatedRel = false;
                }
                getOrCreateListsUpdatesForNodeOffset(direction, boundNodeID)
                    ->updatedPersistentListOffsets.at(listsUpdateInfo.propertyID)
                    .insertOffset(direction == FWD ? listsUpdateInfo.fwdListOffset :
                                                     listsUpdateInfo.bwdListOffset,
                        listsUpdates.at(listsUpdateInfo.propertyID)->getNumTuples() -
                            1 /* ftTupleIdx */);
            } else {
                // If the rel is stored in updateStore (e.g. a newly inserted rel inside the current
                // transaction), we should update the factorizedTable entry for that newly inserted
                // rel.
                auto ftTupleIdx = ftOfInsertedRels->findValueInFlatColumn(
                    INTERNAL_REL_ID_IDX_IN_FT, listsUpdateInfo.relID);
                ftOfInsertedRels->updateFlatCell(ftOfInsertedRels->getTuple(ftTupleIdx),
                    propertyIDToColIdxMap.at(listsUpdateInfo.propertyID),
                    listsUpdateInfo.propertyVector.get(),
                    listsUpdateInfo.propertyVector->state->selVector->selectedPositions[0]);
            }
        }
    }
}

void ListsUpdatesStore::readUpdatesToPropertyVectorIfExists(ListFileID& listFileID,
    node_offset_t nodeOffset, const shared_ptr<ValueVector>& valueVector,
    list_offset_t startListOffset) {
    // Note: only rel property lists can have updates.
    assert(listFileID.listType == ListType::REL_PROPERTY_LISTS);
    auto listsUpdatesForNodeOffset = getListsUpdatesForNodeOffsetIfExists(listFileID, nodeOffset);
    if (listsUpdatesForNodeOffset == nullptr) {
        return;
    }
    auto& updatedPersistentListOffsets = listsUpdatesForNodeOffset->updatedPersistentListOffsets.at(
        listFileID.relPropertyListID.propertyID);
    for (auto& [listOffset, ftTupleIdx] : updatedPersistentListOffsets.listOffsetFTIdxMap) {
        if (startListOffset > listOffset) {
            continue;
        } else if (startListOffset + valueVector->state->originalSize <= listOffset) {
            return;
        }
        auto elemPosInVector = listOffset - startListOffset;
        listsUpdates.at(listFileID.relPropertyListID.propertyID)
            ->copySingleValueToVector(
                ftTupleIdx, LISTS_UPDATES_IDX_IN_FT, valueVector, elemPosInVector);
    }
}

void ListsUpdatesStore::initNewlyAddedNodes(nodeID_t& nodeID) {
    for (auto direction : REL_DIRECTIONS) {
        if (listsUpdatesPerTablePerDirection[direction].contains(nodeID.tableID)) {
            auto& listsUpdatesPerNode =
                listsUpdatesPerTablePerDirection[direction][nodeID.tableID]
                                                [StorageUtils::getListChunkIdx(nodeID.offset)];
            if (!listsUpdatesPerNode.contains(nodeID.offset)) {
                listsUpdatesPerNode.emplace(
                    nodeID.offset, make_unique<ListsUpdatesForNodeOffset>(relTableSchema));
            }
            listsUpdatesPerNode.at(nodeID.offset)->isNewlyAddedNode = true;
        }
    }
}

void ListsUpdatesStore::initInsertedRels() {
    auto factorizedTableSchema = make_unique<FactorizedTableSchema>();
    // The first two columns of factorizedTable are for srcNodeID and dstNodeID.
    factorizedTableSchema->appendColumn(
        make_unique<ColumnSchema>(false /* isUnflat */, 0 /* dataChunkPos */, sizeof(nodeID_t)));
    factorizedTableSchema->appendColumn(
        make_unique<ColumnSchema>(false /* isUnflat */, 0 /* dataChunkPos */, sizeof(nodeID_t)));
    for (auto& relProperty : relTableSchema.properties) {
        auto numBytesForProperty = Types::getDataTypeSize(relProperty.dataType);
        propertyIDToColIdxMap.emplace(
            relProperty.propertyID, factorizedTableSchema->getNumColumns());
        factorizedTableSchema->appendColumn(make_unique<ColumnSchema>(
            false /* isUnflat */, 0 /* dataChunkPos */, numBytesForProperty));
        // Note: we create one factorizedTable to store the updated properties for each property.
        auto updatedRelsSchema = make_unique<FactorizedTableSchema>();
        updatedRelsSchema->appendColumn(make_unique<ColumnSchema>(
            false /* isUnflat */, 0 /* dataChunkPos */, numBytesForProperty));
        listsUpdates.emplace(relProperty.propertyID,
            make_unique<FactorizedTable>(&memoryManager, std::move(updatedRelsSchema)));
    }
    ftOfInsertedRels =
        make_unique<FactorizedTable>(&memoryManager, std::move(factorizedTableSchema));
}

ft_col_idx_t ListsUpdatesStore::getColIdxInFT(ListFileID& listFileID) const {
    if (listFileID.listType == ListType::ADJ_LISTS) {
        return listFileID.adjListsID.relNodeTableAndDir.dir == FWD ? DST_TABLE_ID_IDX_IN_FT :
                                                                     SRC_TABLE_ID_IDX_IN_FT;
    } else {
        return propertyIDToColIdxMap.at(listFileID.relPropertyListID.propertyID);
    }
}

void ListsUpdatesStore::initListsUpdatesPerTablePerDirection() {
    listsUpdatesPerTablePerDirection.clear();
    for (auto direction : REL_DIRECTIONS) {
        listsUpdatesPerTablePerDirection.emplace_back();
        auto boundTableIDs = direction == RelDirection::FWD ?
                                 relTableSchema.getUniqueSrcTableIDs() :
                                 relTableSchema.getUniqueDstTableIDs();
        if (relTableSchema.isStoredAsLists(direction)) {
            for (auto boundTableID : boundTableIDs) {
                listsUpdatesPerTablePerDirection[direction].emplace(
                    boundTableID, ListsUpdatesPerChunk{});
            }
        }
    }
}

ListsUpdatesForNodeOffset* ListsUpdatesStore::getOrCreateListsUpdatesForNodeOffset(
    RelDirection relDirection, nodeID_t nodeID) {
    auto nodeOffset = nodeID.offset;
    auto& listsUpdatesPerNodeOffset = listsUpdatesPerTablePerDirection[relDirection].at(
        nodeID.tableID)[StorageUtils::getListChunkIdx(nodeOffset)];
    if (!listsUpdatesPerNodeOffset.contains(nodeOffset)) {
        listsUpdatesPerNodeOffset.emplace(
            nodeOffset, make_unique<ListsUpdatesForNodeOffset>(relTableSchema));
    }
    return listsUpdatesPerNodeOffset.at(nodeOffset).get();
}

ListsUpdatesForNodeOffset* ListsUpdatesStore::getListsUpdatesForNodeOffsetIfExists(
    ListFileID& listFileID, node_offset_t nodeOffset) const {
    auto relNodeTableAndDir = getRelNodeTableAndDirFromListFileID(listFileID);
    auto& listsUpdatesPerChunk = listsUpdatesPerTablePerDirection[relNodeTableAndDir.dir].at(
        relNodeTableAndDir.srcNodeTableID);
    auto chunkIdx = StorageUtils::getListChunkIdx(nodeOffset);
    if (!listsUpdatesPerChunk.contains(chunkIdx) ||
        !listsUpdatesPerChunk.at(chunkIdx).contains(nodeOffset)) {
        return nullptr;
    }
    return listsUpdatesPerChunk.at(chunkIdx).at(nodeOffset).get();
}

} // namespace storage
} // namespace kuzu
