#include <cstdio>

#include "placer.h"

int main()
{
    ElectricMesh2D m(
        POS_2D{0,0},
        POS_2D{10,10},
        8,
        8
    );
    CRect c1;
    c1.ll = POS_2D{1,2};
    c1.ur = POS_2D{3,4};
    CRect c2;
    c2.ll = POS_2D{4,1};
    c2.ur = POS_2D{7,2};
    m.addModuleDensity(c1,0.9);
    m.addFillerDensity(c2,1);
    m.show();
    return 0;
}