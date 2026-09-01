// This file is part of Projecteur - https://github.com/jahnf/projecteur
// - See LICENSE.md and README.md

#include "spotshapes.h"

#include <QSGFlatColorMaterial>
#include <QSGGeometryNode>

#include <cmath>

namespace {
  const bool registered = [](){
    SpotShapeStar::qmlRegister();
    SpotShapeNGon::qmlRegister();
    return true;
  }();
} // end anonymous namespace

SpotShapeStar::SpotShapeStar(QQuickItem* parent) : QQuickItem (parent)
{
  setEnabled(false);
  setFlags(QQuickItem::ItemHasContents);
}

int SpotShapeStar::qmlRegister()
{
  return qmlRegisterType<SpotShapeStar>("Projecteur.Shapes", 1, 0, "Star");
}

QSGNode* SpotShapeStar::updatePaintNode(QSGNode* oldNode, UpdatePaintNodeData* updatePaintNodeData)
{
  if (width() <= 0 || height() <= 0 || m_color.alpha() == 0) {
    delete oldNode;
    return nullptr;
  }

  // Directly access the QSG transformnode for the Items node: updatePaintNodeData->transformNode->...;
  Q_UNUSED(updatePaintNodeData)

  const auto vertexCount = m_points*2+2;

  // Create geometry node for colored shape
  auto geometryNode = static_cast<QSGGeometryNode *>(oldNode);
  if (geometryNode == nullptr)
  {
    geometryNode = new QSGGeometryNode();

    // Set geometry
    const auto geometry = new QSGGeometry(QSGGeometry::defaultAttributes_Point2D(), vertexCount);

    geometry->setDrawingMode(QSGGeometry::DrawTriangleFan);
    geometryNode->setGeometry(geometry);
    geometryNode->setFlag(QSGNode::OwnsGeometry, true);

    auto* const material = new QSGFlatColorMaterial();
    material->setColor(m_color);
    geometryNode->setMaterial(material);
    geometryNode->setFlag(QSGNode::OwnsMaterial);
  }
  else {
    const auto geometry = geometryNode->geometry();
    if (geometry->vertexCount() != vertexCount) {
      geometry->allocate(vertexCount);
    }
    if (auto material = static_cast<QSGFlatColorMaterial*>(geometryNode->material())) {
      material->setColor(m_color);
    }
  }

  QSGGeometry::Point2D* const vertices = geometryNode->geometry()->vertexDataAsPoint2D();
  const int numSegments = m_points * 2;
  const auto cx = static_cast<float>(width()/2); // center X
  const auto cy = static_cast<float>(height()/2); // center Y
  const auto deltaRad = static_cast<float>((360.0 / m_points) * (M_PI/180.0));
  float theta = -static_cast<float>(90.0 * M_PI/180.0);

  vertices[0].set(cx, cy);
  // Vertices for (outer) star points
  for(int seg=1; seg < numSegments; seg+=2, theta+=deltaRad)
  {
    const float x = cx * cosf(theta);
    const float y = cy * sinf(theta);
    vertices[seg].set(x + cx, y + cy);
  }

  const float dist0_1 = std::sqrt(std::pow(vertices[0].x-vertices[1].x, 2.0f)
                                  + std::pow(vertices[0].y-vertices[1].y, 2.0f));
  const float dist1_3_2 = std::sqrt(std::pow(vertices[1].x-vertices[3].x, 2.0f)
                                    + std::pow(vertices[1].y-vertices[3].y, 2.0f)) / 2.0f;

  const float maxInnerDist = std::sqrt(std::pow(dist0_1,2.0f) - std::pow(dist1_3_2, 2.0f));
  const float innerDistance = maxInnerDist * float(m_innerRadius)/100.0f;

  // Vertices for inner radius
  theta = -static_cast<float>(90.0 * M_PI/180.0) + deltaRad/2 ;
  for(int seg=2; seg < numSegments+1; seg+=2, theta+=deltaRad)
  {
    const float x = innerDistance * std::cos(theta);
    const float y = innerDistance * std::sin(theta);
    vertices[seg].set(x + cx, y + cy);
  }

  vertices[vertexCount-1] = vertices[1]; // last star point = first star point

  geometryNode->markDirty(QSGGeometryNode::DirtyGeometry);
  return geometryNode;
}

QColor SpotShapeStar::color() const
{
  return m_color;
}

void SpotShapeStar::setColor(const QColor &color)
{
  if (m_color == color)
    return;

  m_color = color;
  emit colorChanged(color);
  update(); // redraw, schedules updatePaintNode()...
}

int SpotShapeStar::points() const
{
  return m_points;
}

void SpotShapeStar::setPoints(int points)
{
  if (m_points == points)
    return;

  m_points = qMin(qMax(3, points), 100);
  emit pointsChanged(m_points);
  update(); // redraw, schedules updatePaintNode()...
}

int SpotShapeStar::innerRadius() const
{
  return m_innerRadius;
}

void SpotShapeStar::setInnerRadius(int radiusPercentage)
{
  if (radiusPercentage > m_innerRadius || radiusPercentage < m_innerRadius)
  {
    m_innerRadius = qMin(qMax(5, radiusPercentage), 100);
    emit innerRadiusChanged(m_innerRadius);
    update(); // redraw, schedules updatePaintNode()...
  }
}

SpotShapeNGon::SpotShapeNGon(QQuickItem* parent) : QQuickItem (parent)
{
  setEnabled(false);
  setFlags(QQuickItem::ItemHasContents);
}

int SpotShapeNGon::qmlRegister()
{
  return qmlRegisterType<SpotShapeNGon>("Projecteur.Shapes", 1, 0, "NGon");
}

QColor SpotShapeNGon::color() const
{
  return m_color;
}

void SpotShapeNGon::setColor(const QColor &color)
{
  if (m_color == color)
    return;

  m_color = color;
  emit colorChanged(color);
  update(); // redraw, schedules updatePaintNode()...
}

int SpotShapeNGon::sides() const
{
  return m_sides;
}

void SpotShapeNGon::setSides(int sides)
{
  if (m_sides == sides)
    return;

  m_sides = qMin(qMax(3, sides), 100);
  emit sidesChanged(m_sides);
  update(); // redraw, schedules updatePaintNode()...
}

QSGNode* SpotShapeNGon::updatePaintNode(QSGNode* oldNode, UpdatePaintNodeData* updatePaintNodeData)
{
  if (width() <= 0 || height() <= 0 || m_color.alpha() == 0) {
    delete oldNode;
    return nullptr;
  }

  // Directly access the QSG transformnode for the Items node: updatePaintNodeData->transformNode->...;
  Q_UNUSED(updatePaintNodeData)

  const auto vertexCount = m_sides + 2;

  // Create geometry node for colored shape
  auto geometryNode = static_cast<QSGGeometryNode *>(oldNode);
  if (geometryNode == nullptr)
  {
    geometryNode = new QSGGeometryNode();

    // Set geometry
    const auto geometry = new QSGGeometry(QSGGeometry::defaultAttributes_Point2D(), vertexCount);

    geometry->setDrawingMode(QSGGeometry::DrawTriangleFan);
    geometryNode->setGeometry(geometry);
    geometryNode->setFlag(QSGNode::OwnsGeometry, true);

    const auto material = new QSGFlatColorMaterial();
    material->setColor(m_color);
    geometryNode->setMaterial(material);
    geometryNode->setFlag(QSGNode::OwnsMaterial);
  }
  else {
    const auto geometry = geometryNode->geometry();
    if (geometry->vertexCount() != vertexCount) {
      geometry->allocate(vertexCount);
    }
    if (auto material = static_cast<QSGFlatColorMaterial*>(geometryNode->material())) {
      material->setColor(m_color);
    }
  }

  QSGGeometry::Point2D* const vertices = geometryNode->geometry()->vertexDataAsPoint2D();
  const auto cx = static_cast<float>(width()/2); // center X
  const auto cy = static_cast<float>(height()/2); // center Y
  const auto deltaRad = static_cast<float>((360.0 / m_sides) * (M_PI/180.0));
  float theta = -static_cast<float>(90.0 * M_PI/180.0);

  vertices[0].set(cx, cy);
  for(int seg=1; seg < vertexCount; ++seg, theta+=deltaRad)
  {
    const float x = cx * cosf(theta);
    const float y = cy * sinf(theta);
    vertices[seg].set(x + cx, y + cy);
  }
  vertices[vertexCount-1] = vertices[1]; // last shape point = first shape point

  geometryNode->markDirty(QSGGeometryNode::DirtyGeometry);
  return geometryNode;
}
