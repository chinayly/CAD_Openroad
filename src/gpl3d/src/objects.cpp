#include "objects.h"

void Net::addPin(Pin *pin)
{
    netPins.push_back(pin);
}

int Net::getPinCount()
{
    return netPins.size();
}

void Module::addPin(Pin *p)
{
    modulePins.push_back(p);
}

void Module::setInitialPosition(ModulePosition p)
{
    initialPos = p;
}

double Module::getArea()
{
    return area;
}

void Tier::addRow(SiteRow s)
{
    siteRows.push_back(s);
}

void Tier::setCoreRegion()
{
    double bottom = siteRows.front().bottom;
    double top = siteRows.back().bottom + siteRows.back().height;
    double left = siteRows.front().start.x;
    double right = siteRows.front().end.x;

    float curRowArea = 0;
    for (SiteRow curRow : siteRows)
    {
        left = min(left, curRow.start.x);
        right = max(right, curRow.end.x);
        // printf( "right= %g\n", m_coreRgn.right );
        curRowArea = (curRow.end.x - curRow.start.x) * curRow.height;
    }

    coreRegion.ll = POS_2D(left, bottom);
    coreRegion.ur = POS_2D(right, top);

    cout << "Set core region from site info: ";
    coreRegion.Print();
}

POS_2D SiteRow::getLL_2D()
{
    return start;
}

POS_2D SiteRow::getUR_2D()
{
    POS_2D ur_2D = end;
    ur_2D.y += height;
    return ur_2D;
}
