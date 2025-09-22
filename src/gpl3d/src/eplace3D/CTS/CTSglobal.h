#ifndef CTSGLOBAL_H
#define CTSGLOBAL_H
#include <iostream>
#include <fstream>
#include <cassert>
#include <iomanip>
#include <sstream>
#include <memory.h>
#include <string>
#include <vector>
#include <algorithm>
#include <set>
#include <unordered_set>
#include <unordered_map>
#include <map>
#include <cmath>
#include <functional>
#include <limits>
#include <queue>
#include <memory>
#include <ranges>
#include "arghandler.h"
#include "global.h"

#define CTSEPS 1e-6 // 1e-2, for now
#define PI 3.1415926

#define LINEAR_DELAY 0
#define ELMORE_DELAY 1
#define UNIT_CAPACITANCE 0.2//?
#define UNIT_RESISTANCE 0.1//?

namespace CTSGLOBAL
{
    class CTSInterval; 
};

enum REGION
{
    DUMMY,
    UP_LEFT,
    UP_MID,
    UP_RIGHT,
    MID_LEFT,
    MID_MID,
    MID_RIGHT,
    BOTTOM_LEFT,
    BOTTOM_MID,
    BOTTOM_RIGHT
    // 1 2 3
    // 4 5 6
    // 7 8 9
    //  here we start from 1, to be consistent with the sung kyu lym paper
};

enum POSTION_VERTICAL
{
    UP,
    MID_VERTICAL,
    BOTTOM
};

enum POSTION_HORIZONTAL
{
    LEFT,
    MID_HORIZONTAL,
    RIGHT
};

enum DIRECTION
{
    VERTICAL,
    HORIZONTAL
};

inline bool double_equal(double a, double b)
{
    return fabs(a - b) < CTSEPS;
}

inline bool double_greater(double a, double b) // return true if a > b
{
    return a - b > 1.0 * CTSEPS;
}

inline bool double_less(double a, double b) // return true if a < b
{
    return a - b < -1.0 * CTSEPS;
}

inline bool double_lessorequal(double a, double b)
{
    return double_less(a, b) || double_equal(a, b);
}

inline bool double_greaterorequal(double a, double b)
{
    return double_greater(a, b) || double_equal(a, b);
}


class Point_2D
{
public:
    double x, y;
    Point_2D() { SetZero(); }
    Point_2D(double _x, double _y) : x(_x), y(_y)
    {
    }
    void SetZero()
    {
        x = y = 0.0; //!! 0.0!!!!
    }
    bool operator==(const Point_2D &rhs) const { return double_equal(x, rhs.x) && double_equal(y, rhs.y); }
    friend inline std::ostream &operator<<(std::ostream &os, const Point_2D &gp)
    {
        os << "(" << gp.x << "," << gp.y << ")";
        return os;
    }
};

class Rect
{
public:
    Rect()
    {
        Init();
    }
    void Print()
    {
        cout << "lower left: " << ll << " to upper right: " << ur << "\n";
    }
    void Init()
    {
        ll.SetZero();
        ur.SetZero();
    }
    Point_2D ll; // ll: lower left coor
    Point_2D ur; // ur: upper right coor
    double getWidth()
    {
        double width = ur.x - ll.x;
        assert(width > 0.0);
        return width;
    }
    double getHeight()
    {
        double height = ur.y - ll.y;
        assert(height > 0.0);
        return height;
    }
    Point_2D getCenter()
    {
        Point_2D center = ll;
        center.x += 0.5 * this->getWidth();
        center.y += 0.5 * this->getHeight();
        return center;
    }
    double getArea()
    {
        return getHeight() * getWidth();
    }
    bool inside(Point_2D point)
    {
        return double_greaterorequal(point.x,ll.x)&&double_greaterorequal(point.y,ll.y)&&double_lessorequal(point.x,ur.x)&&double_lessorequal(point.y,ur.y);
    }
};

inline double L1Dist(Point_2D p1, Point_2D p2) { return abs(p1.x - p2.x) + abs(p1.y - p2.y); }

class CTSGLOBAL::CTSInterval
{
public:
    CTSInterval()
    {
        Init();
    }
    CTSInterval(double _start, double _end)
    {
        if (double_greater(_start, _end))
        {
            std::swap(_start, _end);
        }
        start = _start;
        end = _end;
    }
    void Init()
    {
        start = 0;
        end = 0;
    }

    bool inside(double xORy)
    {
        return (double_less(start, xORy) && double_less(xORy, end)); //! less, not less or equal!(open interval)
    }

    bool include(CTSInterval interval)
    {
        return (double_lessorequal(this->start, interval.start) && double_lessorequal(interval.end, this->end));
    }

    double start;
    double end;
};

class RectilinearLine
{
public:
    RectilinearLine()
    {
        Init();
    }
    void Init()
    {
        ll.SetZero();
        ur.SetZero();
        direction = -1;
    };

    RectilinearLine(Point_2D point1, Point_2D point2)
    {
        if (double_equal(point1.x, point2.x))
        {
            direction = VERTICAL;
            ll.x = ur.x = point1.x;
            ll.y = min(point1.y, point2.y);
            ur.y = max(point1.y, point2.y);
        }
        else if (double_equal(point1.y, point2.y))
        {
            direction = HORIZONTAL;
            ll.y = ur.y = point1.y;
            ll.x = min(point1.x, point2.x);
            ur.x = max(point1.x, point2.x);
        }
        else
        {
            cerr << "NOT A RECTILINEAR LINE!\n";
            exit(0);
        }
    }

    Point_2D ll; // lower OR left, not lower left because this is a line
    Point_2D ur; // upper OR right
    int direction;
};

struct WAWirelengthGradientCache
{
    POS_2D pinPos;
    VECTOR_2D expPositive, expNegative;
};

inline vector<VECTOR_2D> calcWAWirelengthGradientOneNet(const vector<POS_2D>& pins, VECTOR_2D invertedGamma )
{
    vector<WAWirelengthGradientCache> cache(pins.size());
    // calculate pin position
    for (size_t i = 0; i < pins.size(); i++)
    {
        cache[i].pinPos = pins[i];
    }
    auto [bbmin, bbmax] = getBoundingBox(cache | std::ranges::views::transform([](WAWirelengthGradientCache &c)
                                                                               { return c.pinPos; }));

    // calculate necessary values to assemble the gradient
    VECTOR_2D numeratorPositive, numeratorNegative;
    VECTOR_2D denominatorPositive, denominatorNegative;
    numeratorPositive.SetZero();
    numeratorNegative.SetZero();
    denominatorPositive.SetZero();
    denominatorNegative.SetZero();
    for (size_t i = 0; i < pins.size(); i++)
    {
        const POS_2D &curPinPos = cache[i].pinPos;
        VECTOR_2D &curExpPositive = cache[i].expPositive;
        VECTOR_2D &curExpNegative = cache[i].expNegative;
        VECTOR_2D expMax; // (Xi-Xmax)/gamma in WA model (X/Y/Z)
        VECTOR_2D expMin; // (Xmin-Xi)/gamma in WA model (X/Y/Z)
        expMax.x = (curPinPos.x - bbmax.x) * invertedGamma.x;
        expMin.x = (bbmin.x - curPinPos.x) * invertedGamma.x;
        expMax.y = (curPinPos.y - bbmax.y) * invertedGamma.y;
        expMin.y = (bbmin.y - curPinPos.y) * invertedGamma.y;

        curExpPositive.x = fastExp(expMax.x);
        numeratorPositive.x += curPinPos.x * curExpPositive.x;
        denominatorPositive.x += curExpPositive.x;
        curExpNegative.x = fastExp(expMin.x);
        numeratorNegative.x += curPinPos.x * curExpNegative.x;
        denominatorNegative.x += curExpNegative.x;

        curExpPositive.y = fastExp(expMax.y);
        numeratorPositive.y += curPinPos.y * curExpPositive.y;
        denominatorPositive.y += curExpPositive.y;
        curExpNegative.y = fastExp(expMin.y);
        numeratorNegative.y += curPinPos.y * curExpNegative.y;
        denominatorNegative.y += curExpNegative.y;
    }

    // assemble the gradient
    vector<VECTOR_2D> wirelengthGradient(pins.size());
    for (size_t i = 0; i < pins.size(); i++)
    {
        VECTOR_2D &curWirelengthGradient = wirelengthGradient[i];
        const VECTOR_2D curPinVec(cache[i].pinPos.x, cache[i].pinPos.y);
        const VECTOR_2D &curExpPositive = cache[i].expPositive;
        const VECTOR_2D &curExpNegative = cache[i].expNegative;
        VECTOR_2D &&curExpPositiveDivideGamma = curExpPositive * invertedGamma;
        VECTOR_2D &&curExpNegativeDivideGamma = curExpNegative * invertedGamma;

        VECTOR_2D &&curPinGradientPositiveTerm =
            ((curExpPositive + curExpPositiveDivideGamma * curPinVec) * denominatorPositive - curExpPositiveDivideGamma * numeratorPositive) / (denominatorPositive * denominatorPositive);
        VECTOR_2D &&curPinGradientNegativeTerm =
            ((curExpNegative - curExpNegativeDivideGamma * curPinVec) * denominatorNegative + curExpNegativeDivideGamma * numeratorNegative) / (denominatorNegative * denominatorNegative);
        curWirelengthGradient += (curPinGradientPositiveTerm - curPinGradientNegativeTerm);
    }
    return wirelengthGradient;
}


#endif