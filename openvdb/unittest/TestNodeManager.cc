// Copyright Contributors to the OpenVDB Project
// SPDX-License-Identifier: MPL-2.0

#include <openvdb/Exceptions.h>
#include <openvdb/Types.h>
#include <openvdb/tree/NodeManager.h>
#include <openvdb/tree/LeafManager.h>
#include "util.h" // for unittest_util::makeSphere()
#include <cppunit/extensions/HelperMacros.h>


class TestNodeManager: public CppUnit::TestFixture
{
public:
    void setUp() override { openvdb::initialize(); }
    void tearDown() override { openvdb::uninitialize(); }

    CPPUNIT_TEST_SUITE(TestNodeManager);
    CPPUNIT_TEST(testAll);
    CPPUNIT_TEST(testDynamic);
    CPPUNIT_TEST_SUITE_END();

    void testAll();
    void testDynamic();
};


CPPUNIT_TEST_SUITE_REGISTRATION(TestNodeManager);


namespace {

template<typename TreeT>
struct NodeCountOp {
    NodeCountOp() : nodeCount(TreeT::DEPTH, 0), totalCount(0)
    {
    }
    NodeCountOp(const NodeCountOp&, tbb::split)
        : nodeCount(TreeT::DEPTH, 0), totalCount(0)
    {
    }
    void join(const NodeCountOp& other)
    {
        for (size_t i = 0; i < nodeCount.size(); ++i) {
            nodeCount[i] += other.nodeCount[i];
        }
        totalCount += other.totalCount;
    }
    // do nothing for the root node
    void operator()(const typename TreeT::RootNodeType&)
    {
    }
    // count the internal and leaf nodes
    template<typename NodeT>
    void operator()(const NodeT&)
    {
        ++(nodeCount[NodeT::LEVEL]);
        ++totalCount;
    }
    std::vector<openvdb::Index64> nodeCount;
    openvdb::Index64 totalCount;
};// NodeCountOp

}//unnamed namespace

void
TestNodeManager::testAll()
{
    using openvdb::CoordBBox;
    using openvdb::Coord;
    using openvdb::Vec3f;
    using openvdb::Index64;
    using openvdb::FloatGrid;
    using openvdb::FloatTree;

    const Vec3f center(0.35f, 0.35f, 0.35f);
    const float radius = 0.15f;
    const int dim = 128, half_width = 5;
    const float voxel_size = 1.0f/dim;

    FloatGrid::Ptr grid = FloatGrid::create(/*background=*/half_width*voxel_size);
    FloatTree& tree = grid->tree();
    grid->setTransform(openvdb::math::Transform::createLinearTransform(/*voxel size=*/voxel_size));

    unittest_util::makeSphere<FloatGrid>(Coord(dim), center,
                                         radius, *grid, unittest_util::SPHERE_SPARSE_NARROW_BAND);

    CPPUNIT_ASSERT_EQUAL(4, int(FloatTree::DEPTH));
    CPPUNIT_ASSERT_EQUAL(3, int(openvdb::tree::NodeManager<FloatTree>::LEVELS));

    std::vector<Index64> nodeCount;
    for (openvdb::Index i=0; i<FloatTree::DEPTH; ++i) nodeCount.push_back(0);
    for (FloatTree::NodeCIter it = tree.cbeginNode(); it; ++it) ++(nodeCount[it.getLevel()]);

    //for (size_t i=0; i<nodeCount.size(); ++i) {//includes the root node
    //    std::cerr << "Level=" << i << " nodes=" << nodeCount[i] << std::endl;
    //}

    {// test tree constructor
        openvdb::tree::NodeManager<FloatTree> manager(tree);

        //for (openvdb::Index i=0; i<openvdb::tree::NodeManager<FloatTree>::LEVELS; ++i) {
        //    std::cerr << "Level=" << i << " nodes=" << manager.nodeCount(i) << std::endl;
        //}

        Index64 totalCount = 0;
        for (openvdb::Index i=0; i<FloatTree::RootNodeType::LEVEL; ++i) {//exclude root in nodeCount
            //std::cerr << "Level=" << i << " expected=" << nodeCount[i]
            //          << " cached=" << manager.nodeCount(i) << std::endl;
            CPPUNIT_ASSERT_EQUAL(nodeCount[i], manager.nodeCount(i));
            totalCount += nodeCount[i];
        }
        CPPUNIT_ASSERT_EQUAL(totalCount, manager.nodeCount());

        // test the map reduce functionality
        NodeCountOp<FloatTree> bottomUpOp;
        NodeCountOp<FloatTree> topDownOp;
        manager.reduceBottomUp(bottomUpOp);
        manager.reduceTopDown(topDownOp);
        for (openvdb::Index i=0; i<FloatTree::RootNodeType::LEVEL; ++i) {//exclude root in nodeCount
            CPPUNIT_ASSERT_EQUAL(bottomUpOp.nodeCount[i], manager.nodeCount(i));
            CPPUNIT_ASSERT_EQUAL(topDownOp.nodeCount[i], manager.nodeCount(i));
        }
        CPPUNIT_ASSERT_EQUAL(bottomUpOp.totalCount, manager.nodeCount());
        CPPUNIT_ASSERT_EQUAL(topDownOp.totalCount, manager.nodeCount());
    }

    {// test DynamicNodeManager
        openvdb::tree::DynamicNodeManager<FloatTree> manager(tree);

        // nodes are not stored on node manager construction
        CPPUNIT_ASSERT_EQUAL(Index64(0), manager.nodeCount());

        // test the map reduce functionality
        NodeCountOp<FloatTree> bottomUpOp;
        NodeCountOp<FloatTree> topDownOp;
        manager.reduceBottomUp(bottomUpOp);
        manager.reduceTopDown(topDownOp);
        for (openvdb::Index i=0; i<FloatTree::RootNodeType::LEVEL; ++i) {//exclude root in nodeCount
            CPPUNIT_ASSERT_EQUAL(bottomUpOp.nodeCount[i], manager.nodeCount(i));
            CPPUNIT_ASSERT_EQUAL(topDownOp.nodeCount[i], manager.nodeCount(i));
        }
        CPPUNIT_ASSERT_EQUAL(bottomUpOp.totalCount, manager.nodeCount());
        CPPUNIT_ASSERT_EQUAL(topDownOp.totalCount, manager.nodeCount());
    }

    {// test LeafManager constructor
        typedef openvdb::tree::LeafManager<FloatTree> LeafManagerT;
        LeafManagerT manager1(tree);
        CPPUNIT_ASSERT_EQUAL(nodeCount[0], Index64(manager1.leafCount()));
        openvdb::tree::NodeManager<LeafManagerT> manager2(manager1);
        Index64 totalCount = 0;
        for (openvdb::Index i=0; i<FloatTree::RootNodeType::LEVEL; ++i) {//exclude root in nodeCount
            //std::cerr << "Level=" << i << " expected=" << nodeCount[i]
            //          << " cached=" << manager2.nodeCount(i) << std::endl;
            CPPUNIT_ASSERT_EQUAL(nodeCount[i], Index64(manager2.nodeCount(i)));
            totalCount += nodeCount[i];
        }
        CPPUNIT_ASSERT_EQUAL(totalCount, Index64(manager2.nodeCount()));

        // test the map reduce functionality
        NodeCountOp<FloatTree> bottomUpOp;
        NodeCountOp<FloatTree> topDownOp;
        manager2.reduceBottomUp(bottomUpOp);
        manager2.reduceTopDown(topDownOp);
        for (openvdb::Index i=0; i<FloatTree::RootNodeType::LEVEL; ++i) {//exclude root in nodeCount
            CPPUNIT_ASSERT_EQUAL(bottomUpOp.nodeCount[i], manager2.nodeCount(i));
            CPPUNIT_ASSERT_EQUAL(topDownOp.nodeCount[i], manager2.nodeCount(i));
        }
        CPPUNIT_ASSERT_EQUAL(bottomUpOp.totalCount, manager2.nodeCount());
        CPPUNIT_ASSERT_EQUAL(topDownOp.totalCount, manager2.nodeCount());
    }

}

namespace {

template<typename TreeT>
struct ExpandOp
{
    using RootT = typename TreeT::RootNodeType;
    using LeafT = typename TreeT::LeafNodeType;

    ExpandOp() = default;

    // do nothing for the root node
    void operator()(RootT&) const { }

    // count the internal and leaf nodes
    template<typename NodeT>
    void operator()(NodeT& node) const
    {
        for (auto iter = node.cbeginValueAll(); iter; ++iter) {
            const openvdb::Coord ijk = iter.getCoord();
            if (ijk.x() < 256 && ijk.y() < 256 && ijk.z() < 256) {
                node.addChild(new typename NodeT::ChildNodeType(iter.getCoord(), NodeT::LEVEL, true));
            }
        }
    }

    void operator()(LeafT& leaf) const
    {
        for (auto iter = leaf.beginValueAll(); iter; ++iter) {
            iter.setValue(iter.pos());
        }
    }
};// ExpandOp

template<typename TreeT>
struct SumOp {
    using RootT = typename TreeT::RootNodeType;

    SumOp() = default;
    SumOp(const SumOp&, tbb::split): totalCount(0) { }
    void join(const SumOp& other)
    {
        totalCount += other.totalCount;
    }
    // do nothing for the root node
    void operator()(const typename TreeT::RootNodeType&) { }
    // count the internal and leaf nodes
    template<typename NodeT>
    void operator()(const NodeT& node)
    {
        for (auto iter = node.cbeginValueAll(); iter; ++iter) {
            totalCount += *iter;
        }
    }
    openvdb::Index64 totalCount = openvdb::Index64(0);
};// SumOp

}//unnamed namespace

void
TestNodeManager::testDynamic()
{
    using openvdb::Coord;
    using openvdb::Index32;
    using openvdb::Index64;
    using openvdb::Int32Tree;

    using RootNodeType = Int32Tree::RootNodeType;
    using Internal1NodeType = RootNodeType::ChildNodeType;

    Int32Tree sourceTree(0);

    auto child =
        std::make_unique<Internal1NodeType>(Coord(0, 0, 0), /*value=*/1.0f);

    CPPUNIT_ASSERT(sourceTree.root().addChild(child.release()));
    CPPUNIT_ASSERT_EQUAL(Index32(0), sourceTree.leafCount());
    CPPUNIT_ASSERT_EQUAL(Index32(2), sourceTree.nonLeafCount());

    ExpandOp<Int32Tree> expandOp;

    { // use NodeManager::foreachTopDown
        Int32Tree tree(sourceTree);
        openvdb::tree::NodeManager<Int32Tree> manager(tree);
        CPPUNIT_ASSERT_EQUAL(Index64(1), manager.nodeCount());
        manager.foreachTopDown(expandOp);
        CPPUNIT_ASSERT_EQUAL(Index32(0), tree.leafCount());

        // first level has been expanded, but node manager cache does not include the new nodes
        SumOp<Int32Tree> sumOp;
        manager.reduceBottomUp(sumOp);
        CPPUNIT_ASSERT_EQUAL(Index64(32760), sumOp.totalCount);
    }

    { // use DynamicNodeManager::foreachBottomUp
        Int32Tree tree(sourceTree);
        openvdb::tree::DynamicNodeManager<Int32Tree> manager(tree);
        CPPUNIT_ASSERT_EQUAL(Index64(0), manager.nodeCount());
        manager.foreachBottomUp(expandOp);
        CPPUNIT_ASSERT_EQUAL(Index32(0), tree.leafCount());

        // first level has been expanded, node manager cache will include the new nodes
        SumOp<Int32Tree> sumOp;
        manager.reduceBottomUp(sumOp);
        CPPUNIT_ASSERT_EQUAL(Index64(98296), sumOp.totalCount);
    }

    { // use DynamicNodeManager::foreachTopDown
        Int32Tree tree(sourceTree);
        openvdb::tree::DynamicNodeManager<Int32Tree> manager(tree);
        CPPUNIT_ASSERT_EQUAL(Index64(0), manager.nodeCount());
        manager.foreachTopDown(expandOp);
        CPPUNIT_ASSERT_EQUAL(Index32(32768), tree.leafCount());

        // all levels have been expanded
        SumOp<Int32Tree> sumOp;
        manager.reduceTopDown(sumOp);
        CPPUNIT_ASSERT_EQUAL(Index64(4286611448), sumOp.totalCount);
    }
}
