#include "util/AcisSatParser.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <string>

/// REQ-320 / ADR-051 (GitHub issue #299): the ACIS SAT parser. No real vendor SAT corpus is
/// available, so every fixture here is hand-authored against the field layout AcisSatParser.cpp
/// documents at its top — the same approach the ADR records as deliberate, not a shortcut.

namespace {

bool Contains(const std::string& haystack, const std::string& needle) {
  return haystack.find(needle) != std::string::npos;
}

/// The mandatory 3 ACIS header lines this parser skips verbatim.
const std::string kHeader =
    "700 0 1 0\n"
    "17 GoSurveyTest 7 32.0.2 NT 24 today\n"
    "1 9.9999999999999995e-07 1e-10\n";

/// A plain cylinder, radius 2, height 5, base at the world origin, axis +Z: 2 planar caps + 1
/// full-revolve cylindrical wall. See AcisSatParser.cpp's field-layout comment for what each
/// record's fields mean; see the ADR-051 (b-1) doc comment on BuildConeFace for the seam synthesis
/// this exercises.
const std::string kCylinderSat = kHeader + R"(
point $-1 2 0 0 #
point $-1 2 0 5 #
vertex $-1 $-1 $0 #
vertex $-1 $-1 $1 #
ellipse-curve $-1 0 0 0 0 0 -1 2 0 0 1 #
ellipse-curve $-1 0 0 5 0 0 1 2 0 0 1 #
edge $-1 $2 $2 $4 forward #
edge $-1 $3 $3 $5 forward #
plane-surface $-1 0 0 0 0 0 -1 1 0 0 #
plane-surface $-1 0 0 5 0 0 1 1 0 0 #
cone-surface $-1 0 0 0 0 0 1 1 0 0 0 1 2 1 #
loop $-1 $-1 $12 $18 #
coedge $-1 $12 $12 $16 $6 forward $11 #
loop $-1 $-1 $14 $19 #
coedge $-1 $14 $14 $17 $7 forward $13 #
loop $-1 $-1 $16 $20 #
coedge $-1 $17 $17 $12 $6 reversed $15 #
coedge $-1 $16 $16 $14 $7 reversed $15 #
face $-1 $19 $11 $21 $8 forward single #
face $-1 $20 $13 $21 $9 forward single #
face $-1 $-1 $15 $21 $10 forward single #
shell $-1 $-1 $-1 $18 $-1 $22 #
lump $-1 $-1 $21 $23 #
body $-1 $22 $-1 $-1 #
End-of-ACIS-data
)";

}  // namespace

TEST_CASE("ACIS SAT import: plain cylinder builds a valid solid", "[acissat]") {
  const acissat::ImportResult r = acissat::ImportSatSolid(kCylinderSat, "TestEntity");
  INFO(r.error);
  REQUIRE(r.ok);
  CHECK(r.solid.faces.size() == 3);
  CHECK(r.solid.shells.size() == 1);

  int planeCount = 0, cylCount = 0;
  for (const brep::Face& f : r.solid.faces) {
    if (f.surface.kind == brep::SurfaceKind::Plane)
      ++planeCount;
    else if (f.surface.kind == brep::SurfaceKind::Cylinder)
      ++cylCount;
  }
  CHECK(planeCount == 2);
  CHECK(cylCount == 1);
  CHECK(brep::Validate(r.solid) == brep::Problem::Ok);

  const auto mp = brep::ComputeMassProperties(r.solid);
  const double expectedVolume = 3.14159265358979323846 * 2.0 * 2.0 * 5.0;
  CHECK(mp.volume == Catch::Approx(expectedVolume).epsilon(1e-9));
}

TEST_CASE("ACIS SAT import: empty stream is refused with a message", "[acissat]") {
  const acissat::ImportResult r = acissat::ImportSatSolid("", "E1");
  CHECK_FALSE(r.ok);
  CHECK_FALSE(r.error.empty());
}

TEST_CASE("ACIS SAT import: unsupported surface kind is refused by name, not silently dropped",
          "[acissat]") {
  // Same cylinder fixture, but the side face's surface record is swapped for a spline-surface stub.
  std::string sat = kCylinderSat;
  const std::string from = "cone-surface $-1 0 0 0 0 0 1 1 0 0 0 1 2 1 #";
  const std::string to = "spline-surface $-1 #";
  const size_t pos = sat.find(from);
  REQUIRE(pos != std::string::npos);
  sat.replace(pos, from.size(), to);

  const acissat::ImportResult r = acissat::ImportSatSolid(sat, "E2");
  CHECK_FALSE(r.ok);
  CHECK(Contains(r.error, "spline-surface"));
  CHECK(Contains(r.error, "E2"));
}

TEST_CASE("ACIS SAT import: sphere-surface is recognized but refused as a fast-follow", "[acissat]") {
  std::string sat = kCylinderSat;
  const std::string from = "cone-surface $-1 0 0 0 0 0 1 1 0 0 0 1 2 1 #";
  const std::string to = "sphere-surface $-1 0 0 0 0 0 1 1 0 0 2 #";
  const size_t pos = sat.find(from);
  REQUIRE(pos != std::string::npos);
  sat.replace(pos, from.size(), to);

  const acissat::ImportResult r = acissat::ImportSatSolid(sat, "");
  CHECK_FALSE(r.ok);
  CHECK(Contains(r.error, "sphere-surface"));
}

TEST_CASE("ACIS SAT import: a wire body (no lump) is refused, not silently empty", "[acissat]") {
  const std::string sat = kHeader + std::string(R"(
body $-1 $-1 $1 $-1 #
wire $-1 #
End-of-ACIS-data
)");
  const acissat::ImportResult r = acissat::ImportSatSolid(sat, "");
  CHECK_FALSE(r.ok);
  CHECK(Contains(r.error, "wire"));
}

TEST_CASE("ACIS SAT import: a malformed record is refused, never crashes", "[acissat]") {
  const std::string sat = kHeader + std::string("body garbage-not-a-pointer #\nEnd-of-ACIS-data\n");
  const acissat::ImportResult r = acissat::ImportSatSolid(sat, "");
  CHECK_FALSE(r.ok);
  CHECK_FALSE(r.error.empty());
}
