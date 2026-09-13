#ifndef PERIMETER_TILEMAPBUMPTILE_H
#define PERIMETER_TILEMAPBUMPTILE_H

static const int bumpGeoScale[TILEMAP_LOD] = { 1, 2, 3, 4, 5 };
static const int bumpTexScale[TILEMAP_LOD] = { 0, 0, 1, 2, 3 };

#define BUMP_VTXTYPE   sVertexXYZT1

class cTileMap;

struct VectDelta: public Vect2i
{
    int delta2;
    Vect2i delta;
    char player;

    enum
    {
        FIX_RIGHT=1,
        FIX_BOTTOM=2,
        FIX_FIXED=4,
        FIX_FIXEMPTY=8,
    };
    char fix;

    void copy_no_fix(const VectDelta& p)
    {
        x=p.x;
        y=p.y;
        delta2=p.delta2;
        delta=p.delta;
        player=p.player;
    }
};

struct sPlayerIB
{
    IndexPoolPage index;
    int nindex;
    int player;
};

struct sBoundaryRegionCandidate
{
    Vect2s point;
    int player;
};

struct sBumpTile
{
    //Only to read
    VertexPoolPage vtx;
    std::vector<sPlayerIB> index;
    // Border-only stitching starts from this immutable terrain grid. Terrain
    // updates rebuild it through Calc(true); Calc(false) copies it before
    // applying its per-border seam changes.
    std::vector<VectDelta> point_base_cache;
    // Region projection for points away from the outer grid lines is likewise
    // invariant across a pure LOD stitch.
    std::vector<VectDelta> interior_point_cache;
    bool interior_point_valid = false;
    // Rebuilt only with the terrain revision; LOD-only updates will project
    // this compact subset instead of traversing all 3x3 region vectors.
    std::vector<sBoundaryRegionCandidate> boundary_region_candidates;
    bool boundary_region_candidates_valid = false;
    // The cells away from the outer one-cell ring are independent of LOD
    // stitching.  Keep their per-player topology until a terrain update;
    // Calc(false) then regenerates only the four boundary strips.
    std::vector<std::vector<sPolygon>> interior_topology_cache;
    bool interior_topology_valid = false;
    // A pure camera-driven LOD stitch changes the boundary vertices but not
    // terrain ownership. Retain the last complete player topology until a
    // terrain revision explicitly invalidates it.
    std::vector<std::vector<sPolygon>> topology_cache;
    bool topology_valid = false;
    Vect2i tile_pos;

    bool init;
    int age, LOD;

    class cTileMap *tilemap;
    class cTilemapTexturePool* texPool;
    int texPage = 0;
    bool initial_texture_reused = false;

    enum
    {
        U_LEFT=0,
        U_RIGHT=1,
        U_TOP=2,
        U_BOTTOM=3,
        U_ALL=4,
    };

    char border_lod[U_ALL];

    void ToList();

protected:
    float vStart, vStep, uStart, uStep;
public:
    sBumpTile(cTileMap* TileMap, cTilemapTexturePool* pool, int lod, int xpos, int ypos);
    ~sBumpTile();
    uint8_t* LockTex(int& Pitch);
    uint8_t* LockVB();
    void UnlockTex();
    void UnlockVB();
    void Calc(bool update_texture, bool reuse_texture = false);
    bool ReuseTextureFrom(sBumpTile& source);
    bool TakeInitialTextureReuse();

    void FindFreeTexture(int& Pool,int& Page,int tex_width,int tex_height);

    inline Vect2f GetUVStart(){return Vect2f(uStart,vStart);};
    inline Vect2f GetUVStep()
    {
        float div=1/(float)(1 << bumpGeoScale[LOD]);
        return Vect2f(uStep * div,vStep*div);
    };

    inline bool IsZeroplast()
    {
        int sz=index.size();
        if(sz>1)
            return true;
        if(sz==1)
            return index[0].player>=0;
        return false;
    }

    inline bool IsOnlyZeroplast()
    {
        return index.size()==1 && index[0].player>=0;
    }
protected:
    void CalcTexture();
    void CalcPoint(bool reuse_point_players, bool invalidate_topology);

    int FixLine(VectDelta* points, int ddv);

    inline float SetVertexZ(TerraInterface* terra,int x,int y);
    void DeleteIndex();
};

#endif //PERIMETER_TILEMAPBUMPTILE_H
