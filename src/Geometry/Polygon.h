#ifndef POLYGON_H
#define POLYGON_H

#include <vector>

#include <boost/geometry/geometries/polygon.hpp>

#include "Shape2D.h"

class Polygon : public Shape2D
{
public:

    Polygon();
    Polygon(const std::vector<Point2D>& vertices);

    //- Polygon properties
    virtual const Point2D& centroid() const { return centroid_; }
    virtual Scalar area() const { return area_; }
    const boost::geometry::model::polygon<Point2D, false, true>& boostPolygon() const { return poly_; }

    //- Tests
    virtual bool isInside(const Point2D& testPoint) const;
    virtual bool isOnEdge(const Point2D& testPoint) const;

    //- Intersections
    virtual Point2D nearestIntersect(const Point2D& testPoint) const;

    virtual void operator+=(const Vector2D& translationVec);
    virtual void operator-=(const Vector2D& translationVec);
    virtual void scale(Scalar factor);
    virtual void rotate(Scalar theta);

    //- Iterators
    auto begin() const { return boost::geometry::exterior_ring(poly_).begin(); }
    auto end() const { return boost::geometry::exterior_ring(poly_).end(); }

protected:

    void init();

    boost::geometry::model::polygon<Point2D, false, true> poly_;

    Point2D centroid_;
    Scalar area_;
};

#endif
