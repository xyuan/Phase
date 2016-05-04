#define BOOST_TEST_MODULE RangeSearchTest

#include <iostream>

#include <boost/test/included/unit_test.hpp>

#include "RectilinearGrid2D.h"
#include "CellSearch.h"

BOOST_AUTO_TEST_SUITE (RangeSearchTest)

BOOST_AUTO_TEST_CASE(RangeSearchTest)
{
    RectilinearGrid2D grid(10, 10, 0.1, 0.1);

    CellSearch cellSearch(grid.activeCells());

    auto result = cellSearch.rangeSearch(Circle(Point2D(0.5, 0.5), 0.3));

    for(const Cell &cell: result)
        std::cout << "Cell centroid: " << cell.centroid() << "\n";
}

BOOST_AUTO_TEST_CASE(KNearestNeighbourSearchTest)
{
    RectilinearGrid2D grid(10, 10, 0.1, 0.1);

    auto result = CellSearch(grid.activeCells()).kNearestNeighbourSearch(Point2D(0.5, 0.5), 4);

    for(const Cell &cell: result)
        std::cout << "Cell centroid: " << cell.centroid() << "\n";
}

BOOST_AUTO_TEST_SUITE_END()
