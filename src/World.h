#pragma once
#include <array>
#include <cstdint>
#include <filesystem>
#include <set>
#include <vector>

namespace vc {
constexpr int MapSize = 512;
constexpr int GridSize = 8;
constexpr int RoadGridSpan = 2;
constexpr int TileSize = GridSize * RoadGridSpan;
constexpr int SidewalkWidth = 1;
constexpr int ChunkTiles = 16;
constexpr int ChunkSize = ChunkTiles * TileSize;
constexpr int ChunksAcross = MapSize / ChunkTiles;
constexpr int ChunkCount = ChunksAcross * ChunksAcross;
constexpr int WorldSize = MapSize * TileSize;
constexpr uint8_t HighwayRule = 16;
constexpr int HighwayWestRow = 204, HighwayEastRow = 208, HighwayJunctionX = 248;

enum Connection : uint8_t { North = 1, East = 2, South = 4, West = 8, NorthEast=16, SouthEast=32, SouthWest=64, NorthWest=128 };
constexpr int RoadDX[]={0,1,0,-1,1,1,-1,-1},RoadDZ[]={-1,0,1,0,-1,1,1,-1};
constexpr int oppositeDirection(int d){return d<4?(d+2)%4:4+(d-4+2)%4;}
struct Cell { int x = -1, z = -1; bool operator==(const Cell&) const = default; };
enum class Material : uint8_t { Grass, Asphalt, Marking, Curb, Sidewalk, Residential, Commercial, Industrial, Utility, Service, Roof, Window, Bad, Good, Warning, HighwayShoulder };
struct Column { int height; Material material; };
struct Vertex { float x,y,z,nx,ny,nz; uint32_t material; };
struct Mesh { std::vector<Vertex> vertices; std::vector<uint32_t> indices; };

struct ParcelVisual { uint8_t kind=0, level=0, variant=0, tint=0; bool operator==(const ParcelVisual&) const = default; };

class World {
public:
    bool road(int x, int z) const;
    uint8_t roadType(Cell c) const {return valid(c.x,c.z)?roads_[c.z*MapSize+c.x]:0;}
    bool roadOccupies(Cell c) const;
    bool setDiagonalRoad(Cell c,bool rising);
    static std::vector<Cell> diagonalLine(Cell from,Cell to);
    size_t diagonalCount() const {return diagonalCount_;}
    uint8_t roadRule(Cell c) const;
    Cell roundaboutOrigin(Cell c) const;
    bool placeRoundabout(Cell origin);
    bool removeRoundabout(Cell c);
    bool highway(Cell c) const {return (roadRule(c)&HighwayRule)!=0;}
    size_t highwayCount() const {return highwayCount_;}
    void generateRegionalHighway();
    void setTileTint(Cell c,uint8_t tint);
    void clearTints();
    const std::vector<uint32_t>& tileStyles() const {return styles_;}
    uint64_t styleRevision() const {return styleRevision_;}
    void setRoadRule(Cell c,uint8_t rule);
    bool canTravel(Cell from,Cell to) const;
    void setParcelVisual(Cell c,ParcelVisual visual);
    void clearParcelVisuals();
    void writeRoads(std::ostream& out) const;
    void readRoads(std::istream& in);
    uint8_t connections(int x, int z) const;
    bool setRoad(int x, int z, bool value);
    void stroke(Cell from, Cell to, bool value);
    Column column(int x, int z) const;
    Mesh mesh(int chunk) const;
    static Mesh parcelMesh(ParcelVisual p,bool simple=false);
    const std::vector<ParcelVisual>& parcels() const {return parcels_;}
    uint64_t parcelRevision() const {return parcelRevision_;}
    bool chunkEmpty(int chunk) const;
    std::set<int> takeDirty();
    void dirtyAll();
    std::set<int> changedVisualChunks(const World* previous) const;
    void markVisualChunks(const std::set<int>& chunks) { dirty_.insert(chunks.begin(),chunks.end()); }
    size_t roadCount() const { return count_; }
    uint64_t topologyRevision() const { return revision_; }
    uint64_t replacementRevision() const { return replacement_; }
    std::vector<Cell> takeTrafficChanges() { auto result=std::move(trafficChanges_); trafficChanges_.clear(); return result; }
    void save(const std::filesystem::path& path) const;
    void load(const std::filesystem::path& path);
    void generateScenario(int scenario);
    static bool valid(int x, int z) { return x >= 0 && z >= 0 && x < MapSize && z < MapSize; }
    static Cell cellAt(float worldX, float worldZ);
private:
    std::array<uint8_t, MapSize * MapSize> roads_{};
    std::vector<uint8_t> rules_=std::vector<uint8_t>(MapSize*MapSize);
    std::vector<ParcelVisual> parcels_=std::vector<ParcelVisual>(MapSize*MapSize);
    std::vector<uint32_t> styles_=std::vector<uint32_t>(MapSize*MapSize);
    uint64_t styleRevision_=0,parcelRevision_=0;
    size_t count_ = 0,highwayCount_=0,diagonalCount_=0;
    std::set<int> dirty_;
    uint64_t revision_=0, replacement_=0;
    std::vector<Cell> trafficChanges_;
};
}
