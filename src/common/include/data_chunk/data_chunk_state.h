#pragma once

#include <cstring>
#include <memory>
#include <vector>

#include "src/common/include/types.h"

using namespace std;

namespace graphflow {
namespace common {

class SharedVectorState {

public:
    SharedVectorState(uint64_t capacity);

    // returns a dataChunkState for vectors holding a single value.
    static shared_ptr<SharedVectorState> getSingleValueDataChunkState();

    inline bool isUnfiltered() const { return selectedPositions == nullptr; }

    inline void resetSelectorToUnselected() { selectedPositions = nullptr; }

    inline void resetSelectorToValuePosBuffer() {
        selectedPositions = selectedPositionsBuffer.get();
    }

    void initMultiplicity() {
        multiplicityBuffer = make_unique<uint64_t[]>(DEFAULT_VECTOR_CAPACITY);
        multiplicity = multiplicityBuffer.get();
    }

    inline bool isFlat() const { return currIdx != -1; }

    inline uint64_t getPositionOfCurrIdx() const {
        return isUnfiltered() ? currIdx : selectedPositions[currIdx];
    }

    inline sel_t getSelectedPositionAtIdx(int i) const {
        return isUnfiltered() ? i : selectedPositions[i];
    }

    uint64_t getNumSelectedValues() const;

    shared_ptr<SharedVectorState> clone();

public:
    // The currIdx is >= 0 when vectors are flattened and -1 if the vectors are unflat.
    int64_t currIdx;
    uint64_t size;
    sel_t* selectedPositions;
    uint64_t* multiplicity;
    unique_ptr<sel_t[]> selectedPositionsBuffer;

private:
    unique_ptr<uint64_t[]> multiplicityBuffer;
};

} // namespace common
} // namespace graphflow
