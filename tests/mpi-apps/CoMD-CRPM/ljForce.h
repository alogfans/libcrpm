/// \file
/// Computes forces for the 12-6 Lennard Jones (LJ) potential.

#ifndef _LJTYPES_H_
#define _LJTYPES_H_

#include <stdio.h>
#include "constants.h"
#include "mytype.h"
#include "parallel.h"
#include "linkCells.h"
#include "memUtils.h"
#include "CoMDTypes.h"


struct BasePotentialSt;
struct BasePotentialSt* initLjPot(void);

/// Write Base Potential structure to file
void writeLJForce(char **data, struct BasePotentialSt* pot);

/// Read Base Potential structure from file
struct BasePotentialSt* readLJForce(char **data);

/// Get checkpoint size
size_t sizeofLJForce();

/// Derived struct for a Lennard Jones potential.
/// Polymorphic with BasePotential.
/// \see BasePotential
typedef struct LjPotentialSt
{
   real_t cutoff;          //!< potential cutoff distance in Angstroms
   real_t mass;            //!< mass of atoms in intenal units
   real_t lat;             //!< lattice spacing (angs) of unit cell
   char latticeType[8];    //!< lattice type, e.g. FCC, BCC, etc.
   char  name[3];	   //!< element name
   int	 atomicNo;	   //!< atomic number  
   int  (*force)(SimFlat* s); //!< function pointer to force routine
   void (*print)(FILE* file, BasePotential* pot);
   void (*destroy)(BasePotential** pot); //!< destruction of the potential
   real_t sigma;
   real_t epsilon;
} LjPotential;


#endif

