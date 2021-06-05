// Authors: Jorge. A Navas, Peter Schachte, Harald Sondergaard, and
//          Peter J. Stuckey.
// The University of Melbourne 2012.
#ifndef __RANGESYNTH__H__
#define __RANGESYNTH__H__
//////////////////////////////////////////////////////////////////////////////
/// \file RangeSynth.h
///       Interval Abstract Domain.
///
/// This file contains the definition of the class RangeSynth,
/// which represents the classical interval abstract domain defined by
/// Cousot&Cousot'76 using fixed-width integers.
///
/// All operations here are sign-dependent. The choice of using
/// signed or unsigned semantics depends on the IsSigned() method in
/// BasicRange.
///
/// About top representation.
///
/// We distinguish between [MIN,MAX] and top. If an interval is top
/// any, e.g., arithmetic operation on it should return directly top
/// rather than doing weird things like performing the operation and
/// then producing overflow. If the interval is [MIN,MAX] still though
/// it has the same information we allow arithmetic operations operate
/// with it, producing possibly overflows.
///////////////////////////////////////////////////////////////////////////////

#include "AbstractValue.h"
#include "BaseRange.h"
#include "llvm/Function.h"
#include "llvm/Module.h"
#include "llvm/BasicBlock.h"
#include "llvm/Instructions.h"
#include "llvm/Attributes.h"
#include "llvm/Constants.h"
#include "llvm/Support/CFG.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/Statistic.h"

#include <tr1/memory>

#define DEBUG_TYPE "RangeSynthAnalysis"
namespace unimelb {

  class RangeSynth;
  typedef std::tr1::shared_ptr<RangeSynth>  RangeSynthPtr;

  /// Widening technique.
  typedef enum {NOWIDEN = 10, COUSOT76 = 11, JUMPSET = 12} WideningOpts;
  const WideningOpts  WideningMethod = JUMPSET; 

  class RangeSynth: public BaseRange {  
  public:          
    virtual BaseId getValueID() const { return RangeId; }

    /// Constructor of the class.
    /// Creates a new object from a Value.
    RangeSynth(Value *V, bool IsSigned): 
      BaseRange(V, IsSigned, true){
      // FIXME: although the code is intended to support also unsigned
      // intervals, we currently have many hooks that assume intervals
      // are signed.
      assert(IsSigned && "Intervals must be signed");
    }
    
    /// Constructor of the class.
    /// Creates a new object from an integer constant.
    RangeSynth(const ConstantInt *C, unsigned Width, bool IsSigned): 
      BaseRange(C, Width, IsSigned, true){ 
      assert(IsSigned && "Intervals must be signed");
    }

    /// Constructor of the class.
    /// Creates a new object from a TBool instance.
    RangeSynth(Value *V, TBool *B, bool IsSigned):
      BaseRange(V, IsSigned, true){
      assert(IsSigned && "Intervals must be signed");
      if (B->isTrue()){
	setLB(1); 
	setUB(1);
      }
      else{
	if (B->isFalse()){
	  setLB(0); 
	  setUB(0);
	}
	else{
	  setLB(0); 
	  setUB(1);
	}
      }
    }
    
    /// Copy constructor of the class.
    RangeSynth(const RangeSynth& other ): 
      BaseRange(other){ }

    /// Constructor of the class for APInt's 
    /// For temporary computations.
    RangeSynth(APInt lb, APInt ub, unsigned Width, bool IsSigned): 
      BaseRange(lb,ub,Width,IsSigned,true){ 
      assert(IsSigned && "Intervals must be signed");
    }
    
    /// Clone method of the class.
    RangeSynth* clone(){
      return new RangeSynth(*this);
    }

    /// To support type inquiry through isa, cast, and dyn_cast.
    static inline bool classof(const RangeSynth *) { 
      return true; 
    }
    static inline bool classof(const BaseRange *V) {
      return (V->getValueID() == RangeId);
    }
    static inline bool classof(const AbstractValue *V) {
      return (V->getValueID() == RangeId);
    }

    /// Destructor of the class.
    ~RangeSynth(){}

    ///////////////////////////////////////////////////////////////////////
    /// Virtual methods defined in BaseRange.h
    ///////////////////////////////////////////////////////////////////////

    inline void setLB(APInt lb){ BaseRange::setLB(lb); }
    virtual inline void setUB(APInt ub){  BaseRange::setUB(ub); }
    virtual inline void setLB(uint64_t lb){  BaseRange::setLB(lb); }
    virtual inline void setUB(uint64_t ub){  BaseRange::setUB(ub); }

    /// Used to compare precision with other analyses
    inline void normalize(){
      if (IsTop()) return;
      if (isBot()) return;
      normalizeTop();
    }

    inline void normalizeTop(){
      if (isBot()) return;
      if (getLB() == getUB()+1) { 
	makeTop();
	return;
      }
    }

    // For comparison with other analyses.
    inline uint64_t Cardinality() const {
      if (isBot()) return 0;
      if (IsTop()) {
	APInt card = APInt::getMaxValue(width);
	return card.getZExtValue() + 1;
      }
	
      APInt x = getLB();
      APInt y = getUB();
      APInt card = (y - x + 1);

      return card.getZExtValue();
    }

    // Standard abstract operations.

    /// Return true if | \gamma(this) | is one.
    virtual bool isGammaSingleton() const{
      return (Cardinality() == 1);
      // if (isBot() || IsTop()) return false;
      // APInt lb  = getLB();
      // APInt ub  = getUB();
      // return (lb == ub);
    }

    virtual bool isBot() const;
    virtual bool IsTop() const;
    virtual void makeBot();
    virtual void makeTop();
    virtual bool lessOrEqual(AbstractValue * V);
    virtual void join(AbstractValue *V);
    virtual void GeneralizedJoin(std::vector<AbstractValue *>){
      llvm_unreachable("This is a lattice so this method should not be called");
    }

    virtual void meet(AbstractValue *V1,AbstractValue *V2);
    virtual bool isEqual(AbstractValue *V);
    virtual void widening(AbstractValue *, const std::vector<int64_t> &); 
		
    /// Return true is this is syntactically identical to V.
    virtual bool isIdentical(AbstractValue *V);

  private:	  
    // Methods to evaluate a guard.
    virtual bool comparisonSle(AbstractValue *);
    virtual bool comparisonSlt(AbstractValue *);
    virtual bool comparisonUle(AbstractValue *);
    virtual bool comparisonUlt(AbstractValue *);

    // Methods to improve bounds from conditionals
    virtual void filterSigma(unsigned, AbstractValue*,AbstractValue*);
    void filterSigma_TwoVars(unsigned, RangeSynth*,RangeSynth*);
    void filterSigma_VarAndConst(unsigned, RangeSynth*,RangeSynth*);

    /////
    // Abstract domain-dependent transfer functions 
    /////

    // addition, substraction, multiplication, signed/unsigned
    // division, and signed/unsigned rem.
    virtual AbstractValue* visitArithBinaryOp(AbstractValue *, AbstractValue *,
					      unsigned, const char *);
    void DoArithBinaryOp(RangeSynth *,RangeSynth *,RangeSynth *,unsigned,const char *,bool &);
    void DoMultiplication(bool, RangeSynth *,RangeSynth *,RangeSynth *,bool &);
    void DoDivision(bool, RangeSynth *, RangeSynth *, RangeSynth *,bool &);
    void DoRem(bool, RangeSynth *, RangeSynth *, RangeSynth *,bool &);
			 
    // and, or, xor, lsh, lshr, ashr
    virtual AbstractValue* 
      visitBitwiseBinaryOp(AbstractValue *,AbstractValue *, 
			   const Type *,const Type *,unsigned, const char *);    
    void DoBitwiseBinaryOp(RangeSynth *,RangeSynth *,RangeSynth *,const Type *,const Type *,unsigned,bool &);
    void DoBitwiseShifts(RangeSynth *, RangeSynth *, RangeSynth *,unsigned, bool &);
    void DoLogicalBitwise(RangeSynth *, RangeSynth *, RangeSynth *,unsigned);
    void signedOr(RangeSynth *  , RangeSynth*);
    void signedAnd(RangeSynth * , RangeSynth*);
    void signedXor(RangeSynth * , RangeSynth*);

    // cast  instructions: truncate and signed/unsigned extension

    bool IsTruncateOverflow(RangeSynth *, unsigned);
    virtual AbstractValue* visitCast(Instruction &,AbstractValue *,TBool*,bool);
    void DoCast(RangeSynth *,RangeSynth *,const Type *,const Type *,const unsigned,bool &);

    bool isCrossingSouthPole(RangeSynth *);
    bool isCrossingNorthPole(RangeSynth *);

    bool comparisonUnsignedLessThan(RangeSynth *, RangeSynth *, bool);

    inline bool comparisonUleDoNotCrossSP(const APInt &a, const APInt &b, 
					  const APInt &c, const APInt &d){
      // [a,b] <= [c,d] if a <= d
      return a.ule(d);
    }
    
    inline bool comparisonUltDoNotCrossSP(const APInt &a, const APInt &b, 
					  const APInt &c, const APInt &d){
      // [a,b] < [c,d] if a <  d
      return a.ult(d);
    }
    
  };

  inline raw_ostream& operator<<(raw_ostream& o, RangeSynth r) {
    r.printRange(o);
    return o;
  }

  // For debugging 
  inline raw_ostream& operator<<(raw_ostream& o, std::vector<RangeSynthPtr> vs) {
    for(unsigned int i=0; i<vs.size();i++){
      o << *(vs[i].get());
    }
    return o;
  }

} // End llvm namespace


#endif
