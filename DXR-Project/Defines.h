#pragma once

// Macro for deleting objects safley
#define SAFEDELETE(OutObject)  if ((OutObject)) { delete (OutObject); (OutObject) = nullptr; }

// Validate (a.k.a ASSERT)
#define VALIDATE(Condition) assert(Condition)

/*
* Forceinline
*/
#ifdef COMPILER_VISUAL_STUDIO
	#define FORCEINLINE __forceinline
#else
	#define FORCEINLINE __attribute__((always_inline)) inline
#endif

/*
* Bit-Mask helpers
*/
#define BIT(Bit)	(1 << Bit)
#define FLAG(Bit)	BIT(Bit)

/*
* Unused params
*/
#define UNREFERENCED_VARIABLE(Variable) (void)(Variable)

/*
* String preprocessor handling
*   There are two versions of PREPROCESS_CONCAT, this is so that you can use __LINE__, __FILE__ etc. within the macro,
*   therefore always use PREPROCESS_CONCAT
*/
#define _PREPROCESS_CONCAT(x, y) x##y
#define PREPROCESS_CONCAT(x, y) _PREPROCESS_CONCAT(x, y)

/*
* Declare warnings as errors
*/

#pragma warning(error : 4456) // variable hides a already existing variable
#pragma warning(error : 4458) // variable hides class member
#pragma warning(error : 4239) // setting references to rvalues
#pragma warning(error : 4715) // not all paths return a value
#pragma warning(error : 4099) // wrong forward declaration
#pragma warning(error : 4150) // cannot call destructor on incomplete type
#pragma warning(error : 4840) // using string in variadic template (When it should be const char)