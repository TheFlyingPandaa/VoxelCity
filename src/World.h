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
// Render height of a raised terrain column; logical road masks remain 0/1.
constexpr float TerrainStepHeight = .25f;
constexpr int ChunkTiles = 16;
constexpr int ChunkSize = ChunkTiles * TileSize;
constexpr int ChunksAcross = MapSize / ChunkTiles;
constexpr int ChunkCount = ChunksAcross * ChunksAcross;
constexpr int WorldSize = MapSize * TileSize;
constexpr uint8_t HighwayRule = 16;
constexpr uint8_t HighwayAccessRule = 32;
constexpr uint8_t ExtendedDirectionRule = 128;
constexpr int HighwayWestRow = 204, HighwayEastRow = 208, HighwayJunctionX = 248;

enum Connection : uint8_t { North = 1, East = 2, South = 4, West = 8, NorthEast=16, SouthEast=32, SouthWest=64, NorthWest=128 };
enum class RoadClass:uint8_t { None,Street,OneWay,Avenue,Highway4,Highway6,LegacyHighway,Median };
struct RoadDefinition {
    const char* name;uint8_t lanesPerDirection;float speed;int cost;float upkeep;uint8_t footprint;bool oneWay,highway,frontage;
};
inline constexpr std::array<RoadDefinition,8> RoadDefinitions{{
    {"None",0,0,0,0,0,false,false,false},
    {"Two-lane street",1,16,20,.01f,1,false,false,true},
    {"Two-lane one-way",2,20,25,.015f,1,true,false,true},
    {"Four-lane avenue",2,24,60,.03f,2,false,false,true},
    {"Four-lane highway",2,32,100,.04f,3,false,true,false},
    {"Six-lane highway",3,36,140,.06f,3,false,true,false},
    {"Legacy regional highway",1,32,0,0,1,true,true,false},
    {"Median",0,0,0,0,1,false,false,false}
}};
constexpr int RoadDX[]={0,1,0,-1,1,1,-1,-1},RoadDZ[]={-1,0,1,0,-1,1,1,-1};
constexpr int oppositeDirection(int d){return d<4?(d+2)%4:4+(d-4+2)%4;}
struct Cell { int x = -1, z = -1; bool operator==(const Cell&) const = default; };
enum class Material : uint8_t { Grass, Asphalt, Marking, Curb, Sidewalk, Residential, Commercial, Industrial, Utility, Service, Roof, Window, Bad, Good, Warning, HighwayShoulder };
constexpr int RegionalRailRow=192;
struct RailTile { uint8_t links=0,flags=0; float height=0; int bridge=-1; };
struct RailBox { float x0,y0,z0,x1,y1,z1; Material material; };
struct Column { int height; Material material; };
struct Vertex { float x,y,z,nx,ny,nz; uint32_t material; };
struct Mesh { std::vector<Vertex> vertices; std::vector<uint32_t> indices; };

struct ParcelVisual { uint8_t kind=0, level=0, variant=0, tint=0; bool operator==(const ParcelVisual&) const = default; };
inline constexpr uint8_t TreeVariantCount=5;
struct TreeInstance { uint32_t tile; float x,z; uint8_t variant; bool operator==(const TreeInstance&) const = default; };
struct TreeBox { float x0,y0,z0,x1,y1,z1; bool leaf; };

class World {
public:
    const RailTile& rail(Cell c) const {static const RailTile empty{};return valid(c.x,c.z)?rails_[c.z*MapSize+c.x]:empty;}
    bool hasRail(Cell c) const {return rail(c).flags!=0;}
    void setRail(Cell c,RailTile value);
    void generateRegionalRailway();
    void clearRails();
    uint64_t railRevision() const {return railRevision_;}
    const std::vector<RailTile>& rails() const {return rails_;}
    std::vector<RailBox> railBoxes(int chunk) const;
    static std::vector<RailBox> railFacilityBoxes(ParcelVisual);
    static std::vector<RailBox> trainBoxes(unsigned kind);
    double railUpkeep() const;
    void writeRails(std::ostream&) const;
    void readRails(std::istream&);
    bool crossingClosed(Cell c) const {return valid(c.x,c.z)&&closedCrossings_[c.z*MapSize+c.x];}
    void closeCrossings(const std::vector<Cell>& cells);
    void initializeVegetation();
    void clearVegetation(Cell c);
    void clearConstructionVegetation(Cell c);
    std::vector<TreeInstance> trees(int chunk) const;
    static std::vector<TreeBox> treeBoxes(uint8_t variant);
    static Mesh treeMesh(uint8_t variant);
    uint64_t vegetationRevision() const { return vegetationRevision_; }
    bool vegetationEnabled() const { return vegetationEnabled_; }
    uint64_t vegetationChunkRevision(int chunk) const { return vegetationChunks_[chunk]; }
    void writeVegetation(std::ostream&) const;
    void readVegetation(std::istream&);
    bool road(int x, int z) const;
    uint8_t roadType(Cell c) const {return valid(c.x,c.z)?roads_[c.z*MapSize+c.x]:0;}
    RoadClass roadClass(Cell c) const {return valid(c.x,c.z)?RoadClass(classes_[c.z*MapSize+c.x]):RoadClass::None;}
    const RoadDefinition& roadDefinition(Cell c) const {return RoadDefinitions[size_t(roadClass(c))];}
    unsigned laneCount(Cell c) const {auto& d=roadDefinition(c);return (d.oneWay||(roadRule(c)&7))?d.lanesPerDirection:d.lanesPerDirection*2;}
    unsigned lanesPerDirection(Cell c) const {return roadDefinition(c).lanesPerDirection;}
    float speedLimit(Cell c) const {return roadDefinition(c).speed;}
    bool zoningFrontage(Cell c) const {return roadDefinition(c).frontage;}
    std::vector<Cell> roadCrossSection(Cell c) const;
    bool roadOccupies(Cell c) const;
    bool setDiagonalRoad(Cell c,bool rising);
    bool setRoadClass(Cell c,RoadClass roadClass);
    static std::vector<Cell> diagonalLine(Cell from,Cell to);
    size_t diagonalCount() const {return diagonalCount_;}
    uint8_t roadRule(Cell c) const;
    Cell roundaboutOrigin(Cell c) const;
    bool placeRoundabout(Cell origin);
    bool removeRoundabout(Cell c);
    bool placeDiamondInterchange(Cell center);
    bool highway(Cell c) const {return (roadRule(c)&HighwayRule)!=0||roadDefinition(c).highway;}
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
    void readRoads(std::istream& in,bool hasClasses=false);
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
    double roadUpkeep() const;
    uint64_t topologyRevision() const { return revision_; }
    uint64_t replacementRevision() const { return replacement_; }
    std::vector<Cell> takeTrafficChanges() { auto result=std::move(trafficChanges_); trafficChanges_.clear(); return result; }
    void save(const std::filesystem::path& path) const;
    void load(const std::filesystem::path& path);
    void generateScenario(int scenario);
    static bool valid(int x, int z) { return x >= 0 && z >= 0 && x < MapSize && z < MapSize; }
    static Cell cellAt(float worldX, float worldZ);
private:
    std::vector<RailTile> rails_=std::vector<RailTile>(MapSize*MapSize);
    std::vector<uint8_t> closedCrossings_=std::vector<uint8_t>(MapSize*MapSize);
    std::vector<int> closedCrossingTiles_;
    uint64_t railRevision_=0;
    bool vegetationEnabled_=false;
    uint32_t landscapeSeed_=1949;
    std::vector<uint8_t> clearedTrees_=std::vector<uint8_t>(MapSize*MapSize/8);
    uint64_t vegetationRevision_=0;
    std::array<uint64_t,ChunkCount> vegetationChunks_{};
    std::array<uint8_t, MapSize * MapSize> roads_{};
    std::vector<uint8_t> classes_=std::vector<uint8_t>(MapSize*MapSize);
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
