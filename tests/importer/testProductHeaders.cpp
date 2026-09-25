#include "doctest.h"
#include "product_importer.hpp"

#include <cstdio>
#include <fstream>

using namespace ob;

TEST_CASE("product importer: duplicate header differing only by case uses the first column") {
    const std::string path = "tests/importer/_tmp_duplicate_header.csv";
    {
        std::ofstream out(path);
        out << "ID,Description,Length,Width,Height,Strength,UoM,Weight,"
               "Cases_Layer,Layers_Unit_Load,Cases_Unit_Load,Pallet_ID,strength\n";
        out << "P1,Test,10,10,10,5,CS,2,4,2,8,TLD,9\n";
    }
    const ProductLoadResult result = ProductImporter::load(path);
    std::remove(path.c_str());

    REQUIRE(result.products.size() == 1);
    CHECK(result.products[0].strength == 5);
}
