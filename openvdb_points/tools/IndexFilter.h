///////////////////////////////////////////////////////////////////////////
//
// Copyright (c) 2015-2016 Double Negative Visual Effects
//
// All rights reserved. This software is distributed under the
// Mozilla Public License 2.0 ( http://www.mozilla.org/MPL/2.0/ )
//
// Redistributions of source code must retain the above copyright
// and license notice and the following restrictions and disclaimer.
//
// *     Neither the name of Double Negative Visual Effects nor the names
// of its contributors may be used to endorse or promote products derived
// from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
// IN NO EVENT SHALL THE COPYRIGHT HOLDERS' AND CONTRIBUTORS' AGGREGATE
// LIABILITY FOR ALL CLAIMS REGARDLESS OF THEIR BASIS EXCEED US$250.00.
//
///////////////////////////////////////////////////////////////////////////
//
/// @file IndexFilter.h
///
/// @authors Dan Bailey
///
/// @brief  Index filters primarily designed to be used with a FilterIndexIter.
///


#ifndef OPENVDB_TOOLS_INDEX_FILTER_HAS_BEEN_INCLUDED
#define OPENVDB_TOOLS_INDEX_FILTER_HAS_BEEN_INCLUDED

#include <openvdb/version.h>
#include <openvdb/Types.h>

#include <openvdb/math/Transform.h>

#include <openvdb_points/tools/IndexIterator.h>
#include <openvdb_points/tools/AttributeArray.h>

namespace openvdb {
OPENVDB_USE_VERSION_NAMESPACE
namespace OPENVDB_VERSION_NAME {
namespace tools {


////////////////////////////////////////


namespace index_filter_internal {

template <typename RandGenT, typename IntType>
struct RandGen
{
    RandGen(const unsigned int _seed, const IntType _length) : randGen(_seed, 0, _length-1), length(_length) { }
    IntType operator()(IntType) {
        IntType result = randGen();
        assert(result >= 0 && result < length);
        return result;
    }
    openvdb::math::RandInt<IntType, RandGenT> randGen;
    IntType length;
};

// generate a random subset of n indices from the range [0:m]
template <typename RandGenT, typename IntType>
void generateRandomSubset(std::vector<IntType>& values, const unsigned int seed, const IntType n, const IntType m)
{
    if (n <= 0)     return;

    if (n >= m) {
        values.reserve(n);
        for (int i = 0; i < int(m); i++)    values.push_back(IntType(i));
        return;
    }

    // reserve and fill vector with ascending indices
    values.reserve(m);
    for (int i = 0; i < int(m); i++)    values.push_back(i);

    // shuffle indices using random generator
    RandGen<RandGenT, IntType> randGen(seed, m);
    std::random_shuffle(values.begin(), values.end(), randGen);

    // resize the container to n elements (does not reduce capacity)
    values.resize(n);

    // sort the subset of the indices vector that will be used
    std::sort(values.begin(), values.end());
}

} // namespace index_filter_internal


// Random index filtering per leaf
template <typename RandGenT>
class RandomLeafFilter
{
public:
    struct Data
    {
        typedef std::pair<Index, Index> SeedCountPair;
        typedef std::map<openvdb::Coord, SeedCountPair> LeafMap;

        Data() { }

        template <typename PointDataTreeT>
        void populateByTargetPoints(const PointDataTreeT& tree,
                                    const Index64 targetPoints,
                                    const unsigned int seed = 0)
        {
            const Index64 currentPoints = pointCount(tree);
            const float factor = targetPoints > currentPoints ? 1.0f : float(targetPoints) / float(currentPoints);

            math::RandInt<unsigned int, boost::mt19937> randGen(seed, 0, std::numeric_limits<unsigned int>::max()-1);

            Index32 leafCounter = 0;
            float totalPointsFloat = 0.0f;
            int totalPoints = 0;
            for (typename PointDataTreeT::LeafCIter iter = tree.cbeginLeaf(); iter; ++iter) {
                // for the last leaf - use the remaining points to reach the target points
                if (leafCounter + 1 == tree.leafCount()) {
                    const int leafPoints = targetPoints - totalPoints;
                    leafMap[iter->origin()] = SeedCountPair(randGen(), leafPoints);
                    break;
                }
                totalPointsFloat += factor * iter->pointCount();
                const int leafPoints = math::Floor(totalPointsFloat);
                totalPointsFloat -= leafPoints;
                totalPoints += leafPoints;

                leafMap[iter->origin()] = SeedCountPair(randGen(), leafPoints);

                leafCounter++;
            }
        }

        template <typename PointDataTreeT>
        void populateByPercentagePoints(const PointDataTreeT& tree,
                                        const float percentage = 10.0f,
                                        const unsigned int seed = 0)
        {
            const int currentPoints = pointCount(tree);
            const int targetPoints = int(math::Round((percentage * currentPoints)/100.0f));

            populateByTargetPoints(tree, targetPoints, seed);
        }

        LeafMap leafMap;
    }; // struct Data

    RandomLeafFilter(const unsigned int seed, const Index count, const Index total)
        : mIndices()
        , mCount(count)
        , mSubsetOffset(-1)
        , mNextIndex(-1)
    {
        using index_filter_internal::generateRandomSubset;
        generateRandomSubset<RandGenT, int>(mIndices, seed, count, total);
    }

    template <typename LeafT>
    static RandomLeafFilter create(const LeafT& leaf, const Data& data) {
        const typename Data::LeafMap::const_iterator it = data.leafMap.find(leaf.origin());
        if (it == data.leafMap.end()) {
            OPENVDB_THROW(openvdb::KeyError, "Cannot find leaf origin in map for random filter - " << leaf.origin());
        }
        const typename Data::SeedCountPair& value = it->second;
        const unsigned int seed = (unsigned int) value.first;
        const Index total = leaf.pointCount();
        const Index count = std::min(value.second, total);
        return RandomLeafFilter(seed, count, total);
    }

    inline void next() const {
        const_cast<int&>(mSubsetOffset)++;
        const_cast<int&>(mNextIndex) =  mSubsetOffset >= mCount ?
                                        std::numeric_limits<int>::max() :
                                        mIndices[mSubsetOffset];
    }

    template <typename IterT>
    bool valid(const IterT& iter) const {
        const int index = *iter;
        while (mNextIndex < index)     this->next();
        return mNextIndex == index;
    }

private:
    std::vector<int> mIndices;
    int mCount;
    int mSubsetOffset;
    int mNextIndex;
}; // class RandomLeafFilter


// BBox index filtering
class BBoxFilter
{
public:
    struct Data
    {
        Data(const openvdb::math::Transform& _transform,
             const openvdb::BBoxd& _bboxWS)
            : transform(_transform)
            , bbox(transform.worldToIndex(_bboxWS)) { }
        const openvdb::math::Transform& transform;
        const openvdb::BBoxd bbox;
    };

    BBoxFilter( const Data& data,
                const AttributeHandle<openvdb::Vec3f>::Ptr& positionHandle)
        : mData(&data)
        , mPositionHandle(positionHandle) { }

    template <typename LeafT>
    static BBoxFilter create(const LeafT& leaf, const Data& data) {
        return BBoxFilter(data, AttributeHandle<openvdb::Vec3f>::create(leaf.constAttributeArray("P")));
    }

    template <typename IterT>
    bool valid(const IterT& iter) const {
        assert(mData);

        const openvdb::Coord ijk = iter.getCoord();
        const openvdb::Vec3f voxelIndexSpace = ijk.asVec3d();

        // Retrieve point position in voxel space
        const openvdb::Vec3f& pointVoxelSpace = mPositionHandle->get(*iter);

        // Compute point position in index space
        const openvdb::Vec3f pointIndexSpace = pointVoxelSpace + voxelIndexSpace;

        return mData->bbox.isInside(pointIndexSpace);
    }

private:
    const Data* mData;
    const AttributeHandle<openvdb::Vec3f>::Ptr mPositionHandle;
}; // class BBoxFilter


// Index filtering based on evaluating both sub-filters
template <typename T1, typename T2, bool And = true>
class BinaryFilter
{
public:
    typedef BinaryFilter<T1, T2, And> FilterT;

    struct Data
    {
        Data(const typename T1::Data& _filterData1,
             const typename T2::Data& _filterData2)
            : filterData1(_filterData1)
            , filterData2(_filterData2) { }
        typename T1::Data filterData1;
        typename T2::Data filterData2;
    };

    BinaryFilter(  const T1& filter1,
                const T2& filter2)
        : mFilter1(filter1)
        , mFilter2(filter2) { }

    template <typename LeafT>
    static FilterT create(const LeafT& leaf, const Data& data) {
        return FilterT( T1::create(leaf, data.filterData1),
                        T2::create(leaf, data.filterData2));
    }

    template <typename IterT>
    bool valid(const IterT& iter) const {
        if (And)      return mFilter1.valid(iter) && mFilter2.valid(iter);
        return mFilter1.valid(iter) || mFilter2.valid(iter);
    }

private:
    const T1 mFilter1;
    const T2 mFilter2;
}; // class BinaryFilter


////////////////////////////////////////


} // namespace tools
} // namespace OPENVDB_VERSION_NAME
} // namespace openvdb


#endif // OPENVDB_TOOLS_INDEX_FILTER_HAS_BEEN_INCLUDED


// Copyright (c) 2015-2016 Double Negative Visual Effects
// All rights reserved. This software is distributed under the
// Mozilla Public License 2.0 ( http://www.mozilla.org/MPL/2.0/ )
