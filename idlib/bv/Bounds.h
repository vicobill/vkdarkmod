/*****************************************************************************
                    The Dark Mod GPL Source Code
 
 This file is part of the The Dark Mod Source Code, originally based 
 on the Doom 3 GPL Source Code as published in 2011.
 
 The Dark Mod Source Code is free software: you can redistribute it 
 and/or modify it under the terms of the GNU General Public License as 
 published by the Free Software Foundation, either version 3 of the License, 
 or (at your option) any later version. For details, see LICENSE.TXT.
 
 Project: The Dark Mod (http://www.thedarkmod.com/)
 
 $Revision$ (Revision of last commit) 
 $Date$ (Date of last commit)
 $Author$ (Author of last commit)
 
******************************************************************************/

#ifndef __BV_BOUNDS_H__
#define __BV_BOUNDS_H__

/*
===============================================================================

	Axis Aligned Bounding Box

===============================================================================
*/

class idBounds {
public:
					idBounds( void );
					explicit idBounds( const idVec3 &mins, const idVec3 &maxs );
					explicit idBounds( const idVec3 &point );

	const idVec3 &	operator[]( const int index ) const;
	idVec3 &		operator[]( const int index );
	idBounds		operator+( const idVec3 &t ) const;				// returns translated bounds
	idBounds &		operator+=( const idVec3 &t );					// translate the bounds
	idBounds		operator*( const idMat3 &r ) const;				// returns rotated bounds
	idBounds &		operator*=( const idMat3 &r );					// rotate the bounds
	idBounds		operator+( const idBounds &a ) const;
	idBounds &		operator+=( const idBounds &a );
	idBounds		operator-( const idBounds &a ) const;
	idBounds &		operator-=( const idBounds &a );

	bool			Compare( const idBounds &a ) const;							// exact compare, no epsilon
	bool			Compare( const idBounds &a, const float epsilon ) const;	// compare with epsilon
	bool			operator==(	const idBounds &a ) const;						// exact compare, no epsilon
	bool			operator!=(	const idBounds &a ) const;						// exact compare, no epsilon

	void			Clear( void );									// inside out bounds
	void			Zero( void );									// single point at origin

	idVec3			GetCenter( void ) const;						// returns center of bounds
	float			GetRadius( void ) const;						// returns the radius relative to the bounds origin
																	// greebo: Note that this does NOT return 0.0 for a
																	// idBounds object with b[0] == b[1] as one might suspect.
																	// I don't know what the intention for this code is, so I'll leave it.
																	// Use GetVolume() instead.
	float			GetRadius( const idVec3 &center ) const;		// returns the radius relative to the given center
	float			GetVolume( void ) const;						// returns the volume of the bounds

	/**
	* Tels: Get the size of the bounds, that is b1 - b0
	*/
	idVec3			GetSize( void ) const;

	bool			IsCleared( void ) const;						// returns true if bounds are inside out

	bool			AddPoint( const idVec3 &v );					// add the point, returns true if the bounds expanded
	bool			AddBounds( const idBounds &a );					// add the bounds, returns true if the bounds expanded
	idBounds		Intersect( const idBounds &a ) const;			// return intersection of this bounds with the given bounds
	idBounds &		IntersectSelf( const idBounds &a );				// intersect this bounds with the given bounds
	idBounds		Expand( const float d ) const;					// return bounds expanded in all directions with the given value
	idBounds &		ExpandSelf( const float d );					// expand bounds in all directions with the given value
	idBounds		Translate( const idVec3 &translation ) const;	// return translated bounds
	idBounds &		TranslateSelf( const idVec3 &translation );		// translate this bounds
	idBounds		Rotate( const idMat3 &rotation ) const;			// return rotated bounds
	idBounds &		RotateSelf( const idMat3 &rotation );			// rotate this bounds

	float			PlaneDistance( const idPlane &plane ) const;
	int				PlaneSide( const idPlane &plane, const float epsilon = ON_EPSILON ) const;

	bool			ContainsPoint( const idVec3 &p ) const;			// includes touching
	bool			IntersectsBounds( const idBounds &a ) const;	// includes touching
	bool			LineIntersection( const idVec3 &start, const idVec3 &end ) const;
					// intersection point is start + dir * scale
	bool			RayIntersection( const idVec3 &start, const idVec3 &dir, float &scale ) const;

					// most tight bounds for the given transformed bounds
	void			FromTransformedBounds( const idBounds &bounds, const idVec3 &origin, const idMat3 &axis );
					// most tight bounds for a point set
	void			FromPoints( const idVec3 *points, const int numPoints );
					// most tight bounds for a translation
	void			FromPointTranslation( const idVec3 &point, const idVec3 &translation );
	void			FromBoundsTranslation( const idBounds &bounds, const idVec3 &origin, const idMat3 &axis, const idVec3 &translation );
					// most tight bounds for a rotation
	void			FromPointRotation( const idVec3 &point, const idRotation &rotation );
	void			FromBoundsRotation( const idBounds &bounds, const idVec3 &origin, const idMat3 &axis, const idRotation &rotation );

	void			ToPoints( idVec3 points[8] ) const;
	idSphere		ToSphere( void ) const;

	const char *		ToString( const int precision = 2 ) const;

	void			AxisProjection( const idVec3 &dir, float &min, float &max ) const;
	void			AxisProjection( const idVec3 &origin, const idMat3 &axis, const idVec3 &dir, float &min, float &max ) const;

private:
	idVec3			b[2];
};

extern idBounds	bounds_zero;
extern idBounds bounds_zeroOneCube; //anon
extern idBounds bounds_unitCube; //anon

ID_INLINE idBounds::idBounds( void ) {
}

ID_INLINE idBounds::idBounds( const idVec3 &mins, const idVec3 &maxs ) {
	b[0] = mins;
	b[1] = maxs;
}

ID_INLINE idBounds::idBounds( const idVec3 &point ) {
	b[0] = point;
	b[1] = point;
}

ID_INLINE const idVec3 &idBounds::operator[]( const int index ) const {
	return b[index];
}

ID_INLINE idVec3 &idBounds::operator[]( const int index ) {
	return b[index];
}

ID_INLINE idBounds idBounds::operator+( const idVec3 &t ) const {
	return idBounds( b[0] + t, b[1] + t );
}

ID_INLINE idBounds &idBounds::operator+=( const idVec3 &t ) {
	b[0] += t;
	b[1] += t;
	return *this;
}

ID_INLINE idBounds idBounds::operator*( const idMat3 &r ) const {
	idBounds bounds;
	bounds.FromTransformedBounds( *this, vec3_origin, r );
	return bounds;
}

ID_INLINE idBounds &idBounds::operator*=( const idMat3 &r ) {
	this->FromTransformedBounds( *this, vec3_origin, r );
	return *this;
}

ID_INLINE idBounds idBounds::operator+( const idBounds &a ) const {
	idBounds newBounds;
	newBounds = *this;
	newBounds.AddBounds( a );
	return newBounds;
}

ID_INLINE idBounds &idBounds::operator+=( const idBounds &a ) {
	idBounds::AddBounds( a );
	return *this;
}

ID_INLINE idBounds idBounds::operator-( const idBounds &a ) const {
	assert( b[1][0] - b[0][0] > a.b[1][0] - a.b[0][0] &&
				b[1][1] - b[0][1] > a.b[1][1] - a.b[0][1] &&
					b[1][2] - b[0][2] > a.b[1][2] - a.b[0][2] );
	return idBounds( idVec3( b[0][0] + a.b[1][0], b[0][1] + a.b[1][1], b[0][2] + a.b[1][2] ),
					idVec3( b[1][0] + a.b[0][0], b[1][1] + a.b[0][1], b[1][2] + a.b[0][2] ) );
}

ID_INLINE idBounds &idBounds::operator-=( const idBounds &a ) {
	assert( b[1][0] - b[0][0] > a.b[1][0] - a.b[0][0] &&
				b[1][1] - b[0][1] > a.b[1][1] - a.b[0][1] &&
					b[1][2] - b[0][2] > a.b[1][2] - a.b[0][2] );
	b[0] += a.b[1];
	b[1] += a.b[0];
	return *this;
}

ID_INLINE bool idBounds::Compare( const idBounds &a ) const {
	return ( b[0].Compare( a.b[0] ) && b[1].Compare( a.b[1] ) );
}

ID_INLINE bool idBounds::Compare( const idBounds &a, const float epsilon ) const {
	return ( b[0].Compare( a.b[0], epsilon ) && b[1].Compare( a.b[1], epsilon ) );
}

ID_INLINE bool idBounds::operator==( const idBounds &a ) const {
	return Compare( a );
}

ID_INLINE bool idBounds::operator!=( const idBounds &a ) const {
	return !Compare( a );
}

ID_INLINE void idBounds::Clear( void ) {
	b[0][0] = b[0][1] = b[0][2] = idMath::INFINITY;
	b[1][0] = b[1][1] = b[1][2] = -idMath::INFINITY;
}

ID_INLINE void idBounds::Zero( void ) {
	b[0][0] = b[0][1] = b[0][2] =
	b[1][0] = b[1][1] = b[1][2] = 0;
}

ID_INLINE idVec3 idBounds::GetCenter( void ) const {
	return idVec3( ( b[1][0] + b[0][0] ) * 0.5f, ( b[1][1] + b[0][1] ) * 0.5f, ( b[1][2] + b[0][2] ) * 0.5f );
}

ID_INLINE float idBounds::GetVolume( void ) const {
	if ( b[0][0] >= b[1][0] || b[0][1] >= b[1][1] || b[0][2] >= b[1][2] ) {
		return 0.0f;
	}
	return ( ( b[1][0] - b[0][0] ) * ( b[1][1] - b[0][1] ) * ( b[1][2] - b[0][2] ) );
}
	
/**
* Tels: Get the size of the bounds, that is b1 - b0
*/
ID_INLINE idVec3 idBounds::GetSize( void ) const {
	return idVec3( b[1][0] - b[0][0], b[1][1] - b[0][1], b[1][2] - b[0][2] );
}
	
ID_INLINE bool idBounds::IsCleared( void ) const {
	return b[0][0] > b[1][0];
}

ID_INLINE bool idBounds::AddPoint( const idVec3 &v ) {
	bool expanded = false;
	if ( v[0] < b[0][0]) {
		b[0][0] = v[0];
		expanded = true;
	}
	if ( v[0] > b[1][0]) {
		b[1][0] = v[0];
		expanded = true;
	}
	if ( v[1] < b[0][1] ) {
		b[0][1] = v[1];
		expanded = true;
	}
	if ( v[1] > b[1][1]) {
		b[1][1] = v[1];
		expanded = true;
	}
	if ( v[2] < b[0][2] ) {
		b[0][2] = v[2];
		expanded = true;
	}
	if ( v[2] > b[1][2]) {
		b[1][2] = v[2];
		expanded = true;
	}
	return expanded;
}

ID_INLINE bool idBounds::AddBounds( const idBounds &a ) {
	bool expanded = false;
	if ( a.b[0][0] < b[0][0] ) {
		b[0][0] = a.b[0][0];
		expanded = true;
	}
	if ( a.b[0][1] < b[0][1] ) {
		b[0][1] = a.b[0][1];
		expanded = true;
	}
	if ( a.b[0][2] < b[0][2] ) {
		b[0][2] = a.b[0][2];
		expanded = true;
	}
	if ( a.b[1][0] > b[1][0] ) {
		b[1][0] = a.b[1][0];
		expanded = true;
	}
	if ( a.b[1][1] > b[1][1] ) {
		b[1][1] = a.b[1][1];
		expanded = true;
	}
	if ( a.b[1][2] > b[1][2] ) {
		b[1][2] = a.b[1][2];
		expanded = true;
	}
	return expanded;
}

ID_INLINE idBounds idBounds::Intersect( const idBounds &a ) const {
	idBounds n;

	// grayman #2734 - Check for intersecting first. Note that if there's
	// no intersection, a bounds with zero min/max points is returned, so
	// if that matters, you should check for that at the spot where this
	// is called.

	n.Zero();
	if (IntersectsBounds(a))
	{
		n.b[0][0] = ( a.b[0][0] > b[0][0] ) ? a.b[0][0] : b[0][0];
		n.b[0][1] = ( a.b[0][1] > b[0][1] ) ? a.b[0][1] : b[0][1];
		n.b[0][2] = ( a.b[0][2] > b[0][2] ) ? a.b[0][2] : b[0][2];
		n.b[1][0] = ( a.b[1][0] < b[1][0] ) ? a.b[1][0] : b[1][0];
		n.b[1][1] = ( a.b[1][1] < b[1][1] ) ? a.b[1][1] : b[1][1];
		n.b[1][2] = ( a.b[1][2] < b[1][2] ) ? a.b[1][2] : b[1][2];
	}
	return n;
}

ID_INLINE idBounds &idBounds::IntersectSelf( const idBounds &a ) {

	// grayman #2734 - Check for intersecting first. Note that if there's
	// no intersection, "this" bounds' max point is set to the min point, so
	// if that matters, you should check for that at the spot where this
	// is called. As of this writing, IntersectSelf() isn't used anywhere.

	if (IntersectsBounds(a))
	{
		if ( a.b[0][0] > b[0][0] )
		{
			b[0][0] = a.b[0][0];
		}
		if ( a.b[0][1] > b[0][1] )
		{
			b[0][1] = a.b[0][1];
		}
		if ( a.b[0][2] > b[0][2] )
		{
			b[0][2] = a.b[0][2];
		}
		if ( a.b[1][0] < b[1][0] )
		{
			b[1][0] = a.b[1][0];
		}
		if ( a.b[1][1] < b[1][1] )
		{
			b[1][1] = a.b[1][1];
		}
		if ( a.b[1][2] < b[1][2] )
		{
			b[1][2] = a.b[1][2];
		}
	}
	else
	{
		b[1] = b[0]; // no intersection, so collapse the bounds
	}

	return *this;
}

ID_INLINE idBounds idBounds::Expand( const float d ) const {
	return idBounds( idVec3( b[0][0] - d, b[0][1] - d, b[0][2] - d ),
						idVec3( b[1][0] + d, b[1][1] + d, b[1][2] + d ) );
}

ID_INLINE idBounds &idBounds::ExpandSelf( const float d ) {
	b[0][0] -= d;
	b[0][1] -= d;
	b[0][2] -= d;
	b[1][0] += d;
	b[1][1] += d;
	b[1][2] += d;
	return *this;
}

ID_INLINE idBounds idBounds::Translate( const idVec3 &translation ) const {
	return idBounds( b[0] + translation, b[1] + translation );
}

ID_INLINE idBounds &idBounds::TranslateSelf( const idVec3 &translation ) {
	b[0] += translation;
	b[1] += translation;
	return *this;
}

ID_INLINE idBounds idBounds::Rotate( const idMat3 &rotation ) const {
	idBounds bounds;
	bounds.FromTransformedBounds( *this, vec3_origin, rotation );
	return bounds;
}

ID_INLINE idBounds &idBounds::RotateSelf( const idMat3 &rotation ) {
	FromTransformedBounds( *this, vec3_origin, rotation );
	return *this;
}

ID_INLINE bool idBounds::ContainsPoint( const idVec3 &p ) const {
	if ( p[0] < b[0][0] || p[1] < b[0][1] || p[2] < b[0][2]
		|| p[0] > b[1][0] || p[1] > b[1][1] || p[2] > b[1][2] ) {
		return false;
	}
	return true;
}

ID_INLINE bool idBounds::IntersectsBounds( const idBounds &a ) const {
	if ( a.b[1][0] < b[0][0] || a.b[1][1] < b[0][1] || a.b[1][2] < b[0][2]
		|| a.b[0][0] > b[1][0] || a.b[0][1] > b[1][1] || a.b[0][2] > b[1][2] ) {
		return false;
	}
	return true;
}

ID_INLINE idSphere idBounds::ToSphere( void ) const {
	idSphere sphere;
	sphere.SetOrigin( ( b[0] + b[1] ) * 0.5f );
	sphere.SetRadius( ( b[1] - sphere.GetOrigin() ).Length() );
	return sphere;
}

ID_INLINE void idBounds::AxisProjection( const idVec3 &dir, float &min, float &max ) const {
	float d1, d2;
	idVec3 center, extents;

	center = ( b[0] + b[1] ) * 0.5f;
	extents = b[1] - center;

	d1 = dir * center;
	d2 = idMath::Fabs( extents[0] * dir[0] ) +
			idMath::Fabs( extents[1] * dir[1] ) +
				idMath::Fabs( extents[2] * dir[2] );

	min = d1 - d2;
	max = d1 + d2;
}

ID_INLINE void idBounds::AxisProjection( const idVec3 &origin, const idMat3 &axis, const idVec3 &dir, float &min, float &max ) const {
	float d1, d2;
	idVec3 center, extents;

	center = ( b[0] + b[1] ) * 0.5f;
	extents = b[1] - center;
	center = origin + center * axis;

	d1 = dir * center;
	d2 = idMath::Fabs( extents[0] * ( dir * axis[0] ) ) +
			idMath::Fabs( extents[1] * ( dir * axis[1] ) ) +
				idMath::Fabs( extents[2] * ( dir * axis[2] ) );

	min = d1 - d2;
	max = d1 + d2;
}

#endif /* !__BV_BOUNDS_H__ */
