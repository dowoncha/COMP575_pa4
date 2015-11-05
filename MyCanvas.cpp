#include "MyCanvas.h"

/**
 * Takes in the bitmap that this canvas will draw to
 * */
MyCanvas::MyCanvas(const GBitmap& bitmap):
	Bitmap(bitmap),
	BmpRect(GIRect::MakeWH(bitmap.width(), bitmap.height()))
{
			/* Push the identity matrix onto the matrix stack */
		MatrixStack.push(GMatrix());
}

MyCanvas::~MyCanvas() {	}

// This function will create my implementation of a Canvas and return it out.
GCanvas* GCanvas::Create(const GBitmap& bitmap)
{
	//If the bitmap width, height are invalid, pixel pointer is null, or the rowbytes size does not match then return NULL
	if (bitmap.width() < 0 || bitmap.height() < 0 || bitmap.pixels() == NULL || bitmap.rowBytes() < (bitmap.width() * sizeof(GPixel)))
		return NULL;

	return new MyCanvas(bitmap);
}

GPixel MyCanvas::Blend(const GPixel src, const GPixel dst)
{
	//If src alpha is 255 then just return the src
  if (GETA(src) == 0xFF)
      return src;
	//If src alpha is 0 then return the dst
	if (GETA(src) == 0)
		return dst;

  //Get the blend alpha value from 255 - Source_Alpha
  COLORBYTE src_a_blend = 0xFF - GETA(src);

  /** Using blend formula for each pixel color ARGB
   *  We first get the value from src, and add it to the result of
   *  multiplying a_blend and the color of dest.
   * */
  COLORBYTE alpha = GETA(src) + MulDiv255Round(src_a_blend, GETA(dst));
  COLORBYTE red = GETR(src) + MulDiv255Round(src_a_blend, GETR(dst));
	red = Utility::clamp((COLORBYTE)0, red, alpha);
  COLORBYTE green = GETG(src) + MulDiv255Round(src_a_blend, GETG(dst));
	green = Utility::clamp((COLORBYTE)0, green, alpha);
  COLORBYTE blue = GETB(src) + MulDiv255Round(src_a_blend, GETB(dst));
	blue = Utility::clamp((COLORBYTE)0, blue, alpha);

  return GPixel_PackARGB(alpha, red, green, blue);

}

void MyCanvas::BlendRow(GPixel *Dst, int startX, GPixel row[], int count)
{
	for ( int i = 0; i < count; ++startX, ++i)
	{
		Dst[startX] = Blend(row[i], Dst[startX]);
	}
}

GPixel MyCanvas::ColorToPixel(const GColor color)
{
    GColor pinned = color.pinToUnit();  //Make sure color is between 0 and 1

    float fA =  pinned.fA * 255.9999;		//Convert from 0-1 to 0-255
    COLORBYTE uA = (COLORBYTE) fA;
    COLORBYTE uR = (COLORBYTE) (pinned.fR * fA);
    COLORBYTE uG = (COLORBYTE) (pinned.fG * fA);
    COLORBYTE uB = (COLORBYTE) (pinned.fB * fA);

    return GPixel_PackARGB(uA, uR, uG, uB);		//Returned the packed pixel
}
/**
 * This function will clear the canvas according to the color.
 * */
void MyCanvas::clear(const GColor& color)
{
  //Get the start of the bitmap _Pixels array
  GPixel* DstPixels = Bitmap.pixels();
  GPixel pColor = ColorToPixel(color);     //Convert input color into a pixel

  for (int y = 0; y < Bitmap.height(); ++y)
    {
      for (int x = 0; x < Bitmap.width(); ++x)
			{
				DstPixels[x] = pColor;
			}

      DstPixels = (GPixel*) ((char*)DstPixels + Bitmap.rowBytes());
    }
}

std::vector<GPoint> MyCanvas::QuadToPoints(const GRect& Rect)
{
	std::vector<GPoint> PreTransform({
		GPoint::Make(Rect.x(), Rect.y()),
	  GPoint::Make(Rect.x() + Rect.width(), Rect.y()),
	  GPoint::Make(Rect.x(), Rect.y() + Rect.height()),
	  GPoint::Make(Rect.x() + Rect.width(), Rect.y() + Rect.height())
	});

	return PreTransform;
}

void MyCanvas::fillRect(const GRect& rect, const GColor& color)
{
	assert(!rect.isEmpty());

	//Convert the color into a shader
	GShader* shader = GShader::FromColor(color);
	//Draw the rect
	shadeRect(rect, shader);
}

void MyCanvas::fillBitmapRect(const GBitmap& src, const GRect& dst)
{
	assert(!dst.isEmpty());

	/* Get the matrix of the conversion from src to dst rect */
	const GMatrix LocalMatrix = RectToRect(GRect::MakeWH(src.width(), src.height()), dst);

	float localArr[6];
	LocalMatrix.GetTwoRows(localArr);
  //Create the shader for a bitmap
	GShader* shader = GShader::FromBitmap(src, localArr);
	//Shade the rectangle with the shader from the bitmap
	shadeRect(dst, shader);
}

void MyCanvas::fillConvexPolygon(const GPoint Points[], const int count, const GColor& color)
{
	assert(count > 2);

	//Make the shader for one color
	GShader* shader = GShader::FromColor(color);

	shadeConvexPolygon(Points, count, shader);										// Fill polygon made by the converted points
}

void MyCanvas::shadeRect(const GRect& rect, GShader* shader)
{
	auto Points = QuadToPoints(rect);

	CTMPoints(Points);

	if ( !MatrixStack.top().preservesRect())
	{
		shadeDevicePolygon(Points, shader);
		return;
	}

	//Convert the CTM'd points back into a rect since we know it preserves it
	auto ConvertedRect = PointsToQuad(Points).round();

	// Make sure rect is not an empty one and clip the edes from the bitmap with intersect
  if (ConvertedRect.isEmpty() || !ConvertedRect.intersect(BmpRect)) {
      return;
	}

	GPixel* DstPixels = Bitmap.pixels();
	DstPixels = (GPixel*)((char*)DstPixels + Bitmap.rowBytes() * ConvertedRect.top());

	float CTM[6];
	MatrixStack.top().GetTwoRows(CTM);

	shader->setContext(CTM);

	int count = ConvertedRect.right() - ConvertedRect.left();
	//All rows will have the same number of pixels, so create a vector of that size
	GPixel* row = new GPixel[count];
	for (int y = ConvertedRect.top(); y < ConvertedRect.bottom(); ++y)
	{
		shader->shadeRow(ConvertedRect.left(), y, count, row);
		//auto row = shader->shadeRow(ConvertedRect.left() , y, count);
		BlendRow(DstPixels, ConvertedRect.left(), row, count);

		DstPixels = (GPixel*)((char*)DstPixels + Bitmap.rowBytes());
	}
}

void MyCanvas::shadeConvexPolygon(const GPoint points[], int count, GShader* shader)
{
	std::vector<GPoint> Points(points, points + count);

	CTMPoints(Points);	//Multiply the Points by the CTM

	shadeDevicePolygon(Points, shader);
}

void MyCanvas::shadeDevicePolygon(std::vector<GPoint>& Points, GShader* shader)
{
	SortPointsForConvex(Points);								//Sort the points into an order we can make edges with
	auto Edges = MakeConvexEdges(Points); //Make edges out of the sorted points
	ClipEdges(Edges);											//Clip the edges from the dst bitmap

	if (Edges.size() < 2)									//If we have less than 2 edges than we cannot draw
	{
		std::cout << "Edges has less than 3 values\n";
		return;
	}

	// Sort the edges from top to bottom, left to right
	std::sort(Edges.begin(), Edges.end());

	// The first two edges will be the top most edges
	GEdge LeftEdge = Edges[0];
	GEdge RightEdge = Edges[1];

	/* Move DstPixels to y offset.*/
	GPixel *DstPixels = Bitmap.pixels();
	// Increment bitmap dst pixel pointer to the start row
	DstPixels = (GPixel*)((char*)DstPixels + Bitmap.rowBytes() * LeftEdge.top());

	float CTM[6];
	MatrixStack.top().GetTwoRows(CTM);

	shader->setContext(CTM);
	int EdgeCounter = 2;
	GPixel* row;
	for (int y = LeftEdge.top(); y < Edges.back().bottom(); ++y)
	{
		int startX = Utility::round(LeftEdge.currentX());
		int count = Utility::round(RightEdge.currentX()) - startX;

		row = new GPixel[count];	      //Allocate array for the row
		shader->shadeRow(startX, y, count, row);
		BlendRow(DstPixels, startX, row, count);

		delete[] row;  //Free the Pixel row since it was dynamically allocated

		// Move left and right edges currentX to next row
		LeftEdge.MoveCurrentX(1.0f);
		RightEdge.MoveCurrentX(1.0f);

		/* Check left edge to see if y has passed bottom and we have not reached max edges */
		if (y > LeftEdge.bottom() && EdgeCounter < Edges.size())
		{
				LeftEdge = Edges[EdgeCounter];
				++EdgeCounter;
		}
		if (y > RightEdge.bottom() && EdgeCounter < Edges.size())
		{
				RightEdge = Edges[EdgeCounter];
				++EdgeCounter;
		}
	}
}

void MyCanvas::save()
{
	MatrixStack.push(MatrixStack.top());	//Push a copy of the CTM onto the stack
}

void MyCanvas::restore()
{
	if (!MatrixStack.empty())		//Check to make sure stack is not empty
	{
		MatrixStack.pop();				//Remove the CTM
	}
}

void MyCanvas::concat(const float matrix[6])
{
	auto& CTM = MatrixStack.top();	//We get a reference to the CTM here to modify it

	/* Make a new matrix using the input matrix values*/
	CTM.concat(GMatrix(matrix));				//Concat the input matrix onto the CTM
}

void MyCanvas::CTMPoints(std::vector<GPoint>& Points) const
{
	auto CTM = GetCTM();

	/* Convert all points by the CTM*/
	for (auto &Point: Points) {
		CTM.convertPoint(Point);
	}
}

GRect MyCanvas::PointsToQuad(const std::vector<GPoint>& Points)
{
	assert(Points.size() == 4);
	/* Return */
	return GRect::MakeLTRB(Points[0].x(), Points[0].y(), Points[3].x(), Points[3].y());
}

GMatrix MyCanvas::RectToRect(const GRect& src, const GRect& dst)
{
	// Make translation matrix of the source from the origin
	auto SrcTranslate = GMatrix::MakeTranslationMatrix(
		-1 * src.left(), -1 * src.top()
	);

	// Make scale matrix of dx, dy. Dst / Src
	auto Scale = GMatrix::MakeScaleMatrix(
		dst.width() / src.width(),
		dst.height() / src.height()
	);

	// Get tranlsation matrix of the dst rectangle
	auto DstTranslate = GMatrix::MakeTranslationMatrix(dst.left(), dst.top());

	// Concatenate the 3 matrices. DstTranslate * Scale * SrcTranslate
	DstTranslate.concat(Scale).concat(SrcTranslate);

	return DstTranslate;
}

void MyCanvas::SortPointsForConvex(std::vector<GPoint>& Points)
{
	/* Sort Points by y and then x if they are equal */
	std::sort(Points.begin(), Points.end(), [](const GPoint& a, const GPoint& b) -> bool{
		return ( std::fabs(a.y() - b.y() ) < .0001) ? a.x() > b.x() : a.y() < b.y();
	});

	/* At this points points are sorted top to bottom, right to left */
	std::queue<GPoint> RightPoints;			//Queue to hold all Points right of the pivot
	std::stack<GPoint> LeftPoints;			//Stack to hold all Points left of the pivot

	/* Start from 2nd value because top is the pivot */
	for (auto Point = Points.begin() + 1; Point != Points.end() - 1; ++Point)
	{
		/* If the x value of the Point is greater than or equal to the top point x*/
		if ( Point->x() >= Points.front().x())
			RightPoints.push(*Point);		//Add to right queue
		else
			LeftPoints.push(*Point);		//Add to left stack
	}

	/* We store the value of RightPoints to get a proper increment later */
	const auto RightSize = RightPoints.size();

	/* Points: Order Diagram - TopPivot, RightPoints ... RightPoints.end() - 1, BottomPivot, LeftPoints ... LeftPoints.end() */
	for (auto Point = Points.begin() + 1; !RightPoints.empty(); ++Point)
	{
		*Point = RightPoints.front();
		RightPoints.pop();
	}

	/* Set the middle point to be the bottom most point */
	Points[RightSize + 1] = Points.back();

	/* If the LeftPoints are not empty store them*/
	for (auto i = RightSize + 2; !LeftPoints.empty(); ++i)
	{
		Points[i] = LeftPoints.top();					//Copy over from left stack
		LeftPoints.pop();											//Pop from left points
	}
}

std::vector<GEdge> MyCanvas::MakeConvexEdges(const std::vector<GPoint>& Points)
{
	std::vector<GEdge> Edges;							//Will hold the edges
	auto Penultimate = --Points.end();		//2nd to last Point

	/* For all values except last connect to adjacent value */
	for (auto Point = Points.begin(); Point != Penultimate; ++Point)
	{
		Edges.emplace_back(*Point, *(Point + 1));			//Create a new edge of two adjacent points
	}

	Edges.emplace_back(Points.front(), Points.back());	// Make an edge of the last point to the first point

	return Edges;
}

void MyCanvas::ClipEdges(std::vector<GEdge>& Edges) const
{
	std::vector<GEdge> NewEdges;			//New edges that will be created from side clipping

	ClipEdgesTopAndBottom(Edges);			//Pin top and bottom edges
	ClipEdgesLeft(Edges, NewEdges);		//Pin left edges, create new edges from clipping
	ClipEdgesRight(Edges, NewEdges);	//Pin right edges, create new edges from clipping

	/* Copy over all clipped new edges into the original edge vector */
	Edges.insert(Edges.end(), NewEdges.begin(), NewEdges.end());
}

void MyCanvas::ClipEdgesTopAndBottom(std::vector<GEdge>& Edges) const
{
	const int height = BmpRect.height();			//The height of the bitmap

	/* Clip all Edges*/
	for (auto Edge = Edges.begin(); Edge != Edges.end(); )
	{
		/* Pin the top and bottom of the edges that are out of bitmap vertical*/
		/* Will return false if the value needs to be removed*/
		if ( !Edge->PinTopAndBot(height) )
		{
			Edge = Edges.erase(Edge);	//Remove all horizontal edges, and edges off screen
		}
		else
		{
			++Edge;		//If we don't remove a value then we increment the iterator
		}
	}
}

void MyCanvas::ClipEdgesLeft(std::vector<GEdge>& Edges, std::vector<GEdge>& NewEdges) const
{
	/* Check all Edges*/
	for (auto &Edge: Edges)
	{
		/* If entire segment is to the left pin it to the edge*/
		if (Edge.currentX() < 0 && Edge.bottomX() < 0)
		{
			Edge.SetCurrentX(0);
			Edge.MakeSlopeVertical();
			continue;
		}

		/* If the top point is to the left of the bitmap*/
		if (Edge.currentX() < 0)
		{
			/* Find the dx between edge and current x and the y point at the new x*/
			float ClipY = Edge.top() - Edge.currentX() / Edge.slope();
			/* Add in new clipped edge*/
			NewEdges.emplace_back(
				GEdge(GPoint::Make(0, Edge.top()),GPoint::Make(0, ClipY))
			);
			/* Move the CurrentX to the edge (0, ClipY)*/
			Edge.SetCurrentX(0);
			/* Change the top of clipped edge from old to new ClipY value*/
			Edge.SetTop((int)(ClipY + .5));				//Potential change in round function to fabs(ClipY + .5)?
		}

		/* If the bottom point of the edge is to the left of the bitmap*/
		if (Edge.bottomX() < 0)
		{
			/* The Clipped y value is the top - currentX (pos) / slope (neg)*/
			float ClipY = Edge.top() - Edge.currentX() / Edge.slope();
			/* Add new clipped edge to NewEdges */
			NewEdges.emplace_back(
				GEdge(GPoint::Make(0, ClipY), GPoint::Make(0, Edge.bottom()))
			);

			Edge.SetBottom((int)(ClipY+.5));		//Set the bottom of the edge to the new clipY
		}
	}
}

void MyCanvas::ClipEdgesRight(std::vector<GEdge>& Edges, std::vector<GEdge>& NewEdges) const
{
	const int width = BmpRect.width();		//Get width of the bitmap

	/* Run for all edges */
	for (auto &Edge: Edges)
	{
		/* If the edge is completely to the right of the bitmap*/
		if (Edge.currentX() > width && Edge.bottomX() > width)
		{
			Edge.SetCurrentX(width);	//Set the CurrentX to the bitmap width
			Edge.MakeSlopeVertical();	//Set the slope to 0. WARNING: This implementation could be better
			continue;									//Go to next edge
		}

		/* If the top of the edge is to the right of the bitmap
		 * Assumptions: Slope: negative, CurrentX > width, BottomX < width */
		if (Edge.currentX() > width)
		{
			/* Get the Y value to clip the edge at*/
			float ClipY = Edge.top() + ( width - Edge.currentX()) / Edge.slope();
			/* Make a new edge from clipped y and old Top*/
			NewEdges.emplace_back(
				GEdge(GPoint::Make(width, Edge.top()), GPoint::Make(width, ClipY))
			);
			Edge.SetCurrentX(width);			//Set the CurrentX to the bitmap width
			Edge.SetTop((int)(ClipY+.5));	//Set the top of clipped edge to the rounded ClipY
		}

		/* If the bottom of the edge needs to be clipped right*/
		/* Assumptions: Slope: positive, CurrentX < width, BottomX > width*/
		if (Edge.bottomX() > width)
		{
			/* Get the ClipY value*/
		  float ClipY = Edge.top() + ( width - Edge.currentX()) / Edge.slope();
			/* Make new edge from the ClipY and the old bottom*/
			NewEdges.emplace_back(
				GEdge(GPoint::Make(width, ClipY), GPoint::Make(width, Edge.bottom()))
			);
			Edge.SetBottom((int)(ClipY + .5));	//Set bottom of clipped edge to rounded ClipY
		}
	}
}
