#ifndef OBJECTS_H
#define OBJECTS_H

#include "global.h"
#include <cstddef>
// todo: use maps for indexing by name

class Module;
class Interval;
class SiteRow;
class Row;
class Pin;
class Net;
class Tier;
class CRect;


class Net
{
public:
    Net() = default;
    Net(string name, vector<Pin *> pins) : name(name), netPins(pins), originalNetName(name) {}
    vector<Pin *> netPins;
    string name;
    string originalNetName;  // 原始 net 名称（用于记录分割前的完整 net 名称）
    void addPin(Pin *);
    int getPinCount();
    // void allocateMemoryForPin(int);
    
    // 时序相关字段（用于AI训练数据标注）
    double slack = 0.0;              // 该net的worst slack（纳秒）
    int iteration = -1;                // 记录slack来自哪个iteration
    bool slack_valid = false;          // slack是否有效
};

enum PinDirection
{
    PIN_DIRECTION_OUT = 0,
    PIN_DIRECTION_IN,
    PIN_DIRECTION_UNDEFINED
};

class Pin
{
public:
    Pin(Module *masterModule, POS_2D offset, PinDirection direction)
        : direction(direction), offset(offset), module(masterModule)
    {
    }
    Module *module;
    POS_2D offset;
    PinDirection direction;
    // void setId(int);
    // void setNet(Net *);
    // void setModule(Module *);
    // void setDirection(int);
};

enum ModuleMoveType
{
    FIXED = 0,
    TIER_FIXED,
    FREE,
    TSV
};

struct ModulePosition
{
    std::size_t tierId;
    POS_2D position;
    ModulePosition() = default;
    ModulePosition(std::size_t tierId, double x, double y) : tierId(tierId)
    {
        position = POS_2D{x, y};
    };
    std::size_t getTierId() const { return tierId; }
    double getX() const { return position.x; }
    double getY() const { return position.y; }
};

class Module
{
public:
    Module() = default;
    Module(string _name, double _width, double _height, ModuleMoveType moveType, bool isNI, bool isMacro = false, bool isTSV = false, bool isTerminal = false)
        : width(_width), height(_height), moveType(moveType), isNI(isNI), isMacro(isMacro), isTSV(isTSV), isTerminal(isTerminal)
    {
        name = _name;
        area = float_mul(width, height); //! area calculated here!
        assert(area >= 0);
    }
    string name;
    bool isNI; // indicate whether the module can be overlapped
    bool isMacro;
    bool isTSV; // indicate whether the module is a TSV
    bool isTerminal; // indicate whether the module is a terminal
    bool isFF = false; // indicate whether the module is a Flipflop
    ModuleMoveType moveType;
    ModulePosition initialPos;
    ModulePosition currentPos; // current position of the module, used in placer
    double width;
    double height;
    double area;
    float clockGradientPreconditioner = 0 ;
    vector<Pin *> modulePins;
    // vector<Net *> nets;
    void addPin(Pin *);
    void setInitialPosition(ModulePosition p);
    double getArea();
};

class Row
{ // an abstract row
    Row()
    {
        bottom = 0;
        height = 0;
        step = 0;
        start.SetZero();
        end.SetZero();
    }

    Row(double _bottom, double _height, double _step) : bottom(_bottom),
                                                        height(_height),
                                                        step(_step)
    {
        start.SetZero();
        end.SetZero();
    }
    double bottom;
    double height;
    double step;
    POS_2D start;
    POS_2D end;
};

class Interval
{
    // horizontal intervals of placement siterows, only store x coordinate, instead of POS_2D
    // there are intervals in rows because of macros or pre-defined dead zones.
    // interval is not an empty! empty is the available space in a row that is not covered by std cells
    // and interval is calculated without considering std cell locations
public:
    Interval()
    {
        SetZero();
    }
    Interval(float _start, float _end)
    {
        start = _start;
        end = _end;
    }
    inline void SetZero()
    {
        start = end = 0.0; //!! 0.0!!!!
    }
    float getLength()
    {
        return end - start;
    }
    float start;
    float end;
};


class SiteRow // a place row
{
public:
    SiteRow()
    {
        intervals.clear();
        bottom = 0;
        height = 0;
        step = 0;
        start.SetZero();
        end.SetZero();
        orientation = OR_N;
    }

    SiteRow(double _bottom, double _height, double _step) : bottom(_bottom),
                                                            height(_height),
                                                            step(_step),
                                                            orientation(OR_N)
    {
        intervals.clear();
        start.SetZero();
        end.SetZero();
    }

    double bottom;              // The bottom y coordinate of this SiteRow of sites
    double height;              // The height of this SiteRow of sites
    double step;                // The minimum x step of SiteRow.	by indark
    POS_2D start;               // lower left coor of this row;
    POS_2D end;                 //! lower right coor of this row;
    ORIENT orientation;         // donnie 2006-04-23  N (0) or S (1)
    vector<Interval> intervals; //!
    // double site_spacing;// site spacing in bookshelf format, equals to site width
    POS_2D getLL_2D();
    POS_2D getUR_2D();
    bool Lesser(SiteRow &r1, SiteRow &r2)
    {
        return (r1.bottom < r2.bottom);
    }
    bool Greater(SiteRow &r1, SiteRow &r2)
    {
        return (r1.bottom > r2.bottom);
    }
    bool isInside(const double &x, const double width)
    {
        // vector<double>::const_iterator ite;
        // for (ite = interval.begin(); ite != interval.end(); ite += 2)
        // {
        //     if (*ite > x)
        //         return false;
        //     // cout << "  sites(" << *ite << " " << *(ite+1) << ") ";
        //     if (*ite <= x && *(ite + 1) >= x)
        //         return true;
        // }
        return false;
    }
    friend inline std::ostream &operator<<(std::ostream &os, const SiteRow &row)
    {
        os << "SiteRow start: " << row.start << " SiteRow end:" << row.end;
        return os;
    }
};

class Tier // one 2D plane(or one 2D chip)
{
public:
    Tier(double substrate_thickness, std::size_t num_metal_layers, double interconnect_layer_thickness,
         double row_height) : substrateThickness(substrate_thickness), numMetalLayers(num_metal_layers),
                              interconnectLayerThickness(interconnect_layer_thickness), rowHeight(row_height)
    {
    }
    CRect coreRegion;
    vector<SiteRow> siteRows;
    double substrateThickness;
    std::size_t numMetalLayers;
    double interconnectLayerThickness;
    double rowHeight;
    void addRow(SiteRow);
    void setCoreRegion();
    double getThickness() { return substrateThickness + interconnectLayerThickness; }
    double getArea() { return coreRegion.getArea(); }
};

#endif