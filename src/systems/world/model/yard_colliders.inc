#define BOX(x0,x1,z0,z1,y0,y1) {((x0)+(x1))*.5f,((z0)+(z1))*.5f,((x1)-(x0))*.5f,((z1)-(z0))*.5f,y0,y1,false}
#define ROUND(x,z,r,y0,y1) {x,z,r,r,y0,y1,true}
#define POST(x,z,r,y1) {x,z,r,r,-.28f,y1,false}
#define TRELLIS(z) \
    BOX(-1.65f,-1.55f,(z)-.76f,(z)+.76f,-.05f,1.85f), \
    BOX(-.05f,.05f,(z)-.76f,(z)+.76f,-.05f,1.85f), \
    BOX(1.55f,1.65f,(z)-.76f,(z)+.76f,-.05f,1.85f), \
    BOX(-2.2f,2.2f,(z)-.045f,(z)+.045f,1.67f,1.74f)

static const struct {
    const char *id;
    int count;
    BuildingCollider boxes[12];
} yard_colliders[]={
    {"house_0001_yard_1_campfire",6,{
        ROUND(-.05f,0,1.1f,-.08f,.28f),
        BOX(-1.17f,-1.03f,-.07f,.07f,-.30f,1.1f),
        BOX(1.03f,1.17f,-.07f,.07f,-.30f,1.1f),
        BOX(-1.25f,1.25f,-.07f,.07f,.87f,.94f),
        ROUND(-1.82f,1.6f,.28f,-.20f,.50f), ROUND(1.62f,1.83f,.27f,-.20f,.50f)
    }},
    {"house_0001_yard_1_woodrack",4,{
        BOX(-1.52f,-1.28f,-1.15f,1.15f,-.24f,1.83f),
        BOX(1.28f,1.52f,-1.15f,1.15f,-.24f,1.83f),
        BOX(-1.7f,1.7f,-.36f,.37f,.67f,1.5f),
        ROUND(1.5f,1.6f,.35f,-.17f,.47f)
    }},
    {"house_0001_yard_1_dryingrack",5,{
        BOX(-2.30f,-2.10f,-1.05f,1.05f,-.30f,2.3f),
        BOX(-.10f,.10f,-1.05f,1.05f,-.30f,2.3f),
        BOX(2.10f,2.30f,-1.05f,1.05f,-.30f,2.3f),
        BOX(-2.4f,2.4f,-.09f,.09f,.83f,1.55f),
        BOX(-2.5f,2.5f,-.07f,.07f,2.04f,2.16f)
    }},
    {"house_0001_yard_1_haystack",3,{
        ROUND(-.8f,-.6f,1.24f,-.10f,2.16f),
        ROUND(1.3f,.43f,.86f,-.10f,1.57f),
        ROUND(-1.6f,1.3f,.63f,-.10f,1.08f)
    }},
    {"house_0001_yard_1_tether",4,{
        ROUND(-.70f,.01f,.19f,-.25f,1.25f), ROUND(.78f,.30f,.20f,-.26f,1.16f),
        BOX(-.78f,.86f,-.04f,.35f,.68f,.83f),
        BOX(-.88f,.88f,-1.74f,-1.06f,-.06f,.28f)
    }},
    {"house_0001_yard_1_stonework",2,{
        ROUND(0,0,1.15f,-.25f,.92f), ROUND(0,1.42f,.28f,-.17f,.47f)
    }},
    {"house_0002_yard_campfire",7,{
        ROUND(0,0,1.34f,-.11f,.36f),
        POST(-1.39f,0,.15f,1.65f), POST(1.39f,0,.15f,1.65f),
        BOX(-1.55f,1.55f,-.06f,.06f,1.31f,1.39f),
        ROUND(-1.81f,1.37f,.27f,-.17f,.61f), ROUND(1.81f,1.31f,.27f,-.17f,.61f),
        ROUND(.09f,-1.92f,.27f,-.18f,.61f)
    }},
    {"house_0002_yard_woodrack",4,{
        BOX(-1.91f,-1.69f,-.64f,.64f,-.28f,1.97f),
        BOX(1.69f,1.91f,-.64f,.64f,-.28f,1.97f),
        BOX(-1.99f,1.99f,-.67f,.67f,.26f,1.25f),
        ROUND(0,1.82f,.34f,-.21f,.65f)
    }},
    {"house_0002_yard_dryingrack",5,{
        BOX(-2.31f,-2.09f,-1.21f,1.23f,-.30f,2.5f),
        BOX(-.11f,.11f,-1.21f,1.23f,-.30f,2.5f),
        BOX(2.09f,2.31f,-1.21f,1.23f,-.30f,2.5f),
        BOX(-2.5f,2.5f,-.13f,.13f,.83f,1.61f),
        BOX(-2.6f,2.6f,-.08f,.08f,2.13f,2.27f)
    }},
    {"house_0002_yard_woodtotem",6,{
        ROUND(0,0,.29f,-.35f,4.05f),
        BOX(-.95f,.95f,-.10f,.10f,3.68f,3.82f),
        ROUND(-1.4f,-1.20f,.19f,-.20f,1.40f), ROUND(1.4f,-1.20f,.19f,-.20f,1.40f),
        ROUND(-1.4f,1.21f,.19f,-.20f,1.40f), ROUND(1.4f,1.21f,.19f,-.20f,1.40f)
    }},
    {"house_0002_yard_weaponposts",6,{
        ROUND(-1.1f,.22f,.36f,-.20f,.90f),
        ROUND(1.2f,-.3f,.42f,-.21f,.65f),
        ROUND(-.2f,-1.38f,.29f,-.20f,1.10f),
        BOX(-.72f,.33f,-1.50f,-.91f,-.17f,2.20f),
        BOX(-2.0f,-.48f,1.08f,2.00f,-.16f,1.55f),
        BOX(.50f,2.0f,.92f,1.95f,-.14f,1.54f)
    }},
    {"house_0002_yard_stonework",2,{
        ROUND(0,0,1.24f,-.25f,.99f), ROUND(0,1.52f,.32f,-.19f,.51f)
    }},
    {"house_0003_yard_campfire",5,{
        ROUND(-.06f,0,1.20f,-.11f,.29f),
        ROUND(0,-.13f,.58f,.29f,.52f),
        BOX(-.29f,.29f,-.26f,.22f,.93f,1.98f),
        ROUND(-1.81f,1.41f,.28f,-.22f,.52f), ROUND(1.71f,1.52f,.27f,-.22f,.52f)
    }},
    {"house_0003_yard_woodrack",4,{
        BOX(-.77f,.77f,-1.51f,-1.32f,-.27f,1.97f),
        BOX(-.77f,.77f,1.32f,1.51f,-.27f,1.97f),
        BOX(-.45f,.45f,-1.70f,1.70f,1.09f,1.92f),
        ROUND(0,2.42f,.34f,-.22f,.53f)
    }},
    {"house_0003_yard_dryingrack",5,{
        BOX(-2.1f,-1.9f,-1.02f,1.04f,-.32f,2.5f),
        BOX(-.10f,.10f,-1.02f,1.04f,-.32f,2.5f),
        BOX(1.9f,2.1f,-1.02f,1.04f,-.32f,2.5f),
        BOX(-2.4f,2.4f,-.10f,.10f,.69f,1.34f),
        BOX(-2.4f,2.4f,-.06f,.06f,2.15f,2.25f)
    }},
    {"house_0003_yard_garden_empty",6,{
        POST(-3,-3,.04f,.70f), POST(3,-3,.04f,.70f),
        POST(-3,3,.04f,.70f), POST(3,3,.04f,.70f),
        ROUND(3.25f,-.69f,.26f,0,.55f),
        BOX(2.89f,3.35f,2.5f,3.15f,-.02f,.74f)
    }},
    {"house_0003_yard_garden_simple_1",2,{
        BOX(-2.95f,-2.85f,.67f,.73f,0,.40f),
        ROUND(2.5f,2.12f,.24f,.17f,.67f)
    }},
    {"house_0003_yard_garden_simple_2",0,{{0}}},
    {"house_0003_yard_garden_simple_3",12,{
        TRELLIS(-1.8f), TRELLIS(0), TRELLIS(1.8f)
    }},
    {"house_0003_yard_stonework",2,{
        ROUND(0,0,1.15f,-.27f,.93f), ROUND(0,1.51f,.28f,-.20f,.50f)
    }},
    {"house_0004_yard_1_campfire",4,{
        ROUND(0,0,1.53f,-.19f,.29f), ROUND(0,0,.43f,.29f,.66f),
        ROUND(-2.22f,.49f,.28f,-.22f,.46f), ROUND(2.1f,-.78f,.27f,-.22f,.42f)
    }},
    {"house_0004_yard_1_coconut_bench",8,{
        POST(.20f,-.60f,.12f,.90f), POST(2,-.60f,.12f,.90f),
        POST(.20f,.60f,.12f,.90f), POST(2,.60f,.12f,.90f),
        BOX(0,2.2f,-.70f,.70f,.85f,.99f),
        ROUND(-1.4f,.01f,.11f,-.30f,.75f),
        ROUND(-1.4f,1.24f,.78f,0,.42f), ROUND(1.1f,1.4f,.23f,-.23f,.33f)
    }},
    {"house_0004_yard_1_dryingrack",5,{
        BOX(-2.31f,-2.09f,-.96f,.96f,-.30f,2.60f),
        BOX(-.11f,.11f,-.96f,.96f,-.30f,2.60f),
        BOX(2.09f,2.31f,-.96f,.96f,-.30f,2.60f),
        BOX(-2.5f,2.5f,-.11f,.11f,1.35f,1.71f),
        BOX(-2.6f,2.6f,-.07f,.07f,2.29f,2.41f)
    }},
    {"house_0004_yard_1_fishing_bench",8,{
        POST(-1.3f,-.50f,.12f,.95f), POST(1.3f,-.50f,.12f,.95f),
        POST(-1.3f,.50f,.12f,.95f), POST(1.3f,.50f,.12f,.95f),
        BOX(-1.60f,1.60f,-.70f,.70f,.90f,1.02f),
        BOX(-1.10f,1.10f,-.45f,.45f,.27f,.33f),
        BOX(-.90f,.90f,1.08f,1.52f,.44f,.52f),
        BOX(-.79f,.79f,1.23f,1.39f,-.22f,.45f)
    }},
    {"house_0004_yard_1_net_rack",7,{
        POST(-2.2f,-1,.12f,2.5f), POST(2.2f,-1,.12f,2.5f),
        POST(-2.2f,1,.12f,2.5f), POST(2.2f,1,.12f,2.5f),
        BOX(-2.35f,2.35f,-1.07f,-.93f,.53f,2.28f),
        BOX(-2.35f,2.35f,.93f,1.08f,.53f,2.28f),
        BOX(-2.2f,2.2f,-1,1,2.24f,2.27f)
    }},
    {"house_0004_yard_1_stone_stove",4,{
        BOX(-1.10f,1.10f,-1.13f,.50f,-.24f,.76f),
        ROUND(0,-.20f,.65f,.68f,1.23f),
        BOX(-2.23f,-1.37f,.57f,1.42f,-.08f,.44f),
        ROUND(1.80f,.8f,.32f,-.20f,.40f)
    }},
    {"house_0004_yard_1_stump_axe_spear",2,{
        ROUND(0,0,.65f,-.30f,.90f),
        BOX(-.86f,.83f,-.39f,.41f,-.24f,1.77f)
    }},
    {"house_0004_yard_1_water_vat",3,{
        ROUND(0,0,.9f,-.28f,1.35f),
        BOX(-1.99f,-.90f,-1.44f,-.55f,-.27f,1.62f),
        BOX(-2.19f,-.46f,-1.53f,-.3f,1.28f,1.96f)
    }},
    {"house_0004_yard_1_woodrack",5,{
        BOX(-1.73f,-1.47f,-.99f,.99f,-.30f,2.60f),
        BOX(1.47f,1.73f,-.99f,.99f,-.30f,2.60f),
        BOX(-1.75f,1.75f,-.85f,.85f,.42f,1.05f),
        BOX(-1.60f,1.60f,-.75f,.75f,1.27f,1.56f),
        BOX(-1.9f,1.9f,-1.10f,1.10f,2.32f,2.96f)
    }}
};

#undef TRELLIS
#undef POST
#undef ROUND
#undef BOX
