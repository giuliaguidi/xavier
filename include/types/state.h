class XavierState
{
public:

	XavierState
	(
		SeedX& _seed,
	 	std::string const& hseq,
		std::string const& vseq,
		ScoringSchemeX& scoringScheme,
		int64_t const &_scoreDropOff
	)
	{
		seed = _seed;

		hlength = hseq.length() + 1;	// + VECTORWIDTH;
		vlength = vseq.length() + 1;	// + VECTORWIDTH;

		if (hlength < VECTORWIDTH || vlength < VECTORWIDTH)
		{
			setEndPositionH(seed, hlength);
			setEndPositionV(seed, vlength);
			//	GG: main function takes care of this
			// 	return;
		}

		// Convert from string to int array
		// This is the entire sequences
		queryh = new int8_t[hlength + VECTORWIDTH];
		queryv = new int8_t[vlength + VECTORWIDTH];

		std::copy(hseq.begin(), hseq.begin() + hlength, queryh);
		std::copy(vseq.begin(), vseq.begin() + vlength, queryv);

		std::fill(queryh + hlength, queryh + hlength + VECTORWIDTH, NINF);
		std::fill(queryv + vlength, queryv + vlength + VECTORWIDTH, NINF);

		matchCost    = scoreMatch(scoringScheme   );
		mismatchCost = scoreMismatch(scoringScheme);
		gapCost      = scoreGap(scoringScheme     );

		vmatchCost    = setOp (matchCost   );
		vmismatchCost = setOp (mismatchCost);
		vgapCost      = setOp (gapCost     );
		vzeros        = _mm256_setzero_si256();

		hoffset = LOGICALWIDTH;
		voffset = LOGICALWIDTH;

		bestScore    = 0;
		currScore    = 0;
		scoreOffset  = 0;
		scoreDropOff = _scoreDropOff;
		xDropCond   = false;
	}

	~XavierState()
	{
		delete [] queryh;
		delete [] queryv;
	}

	// i think this can be smaller than 64bit
	int64_t get_score_offset  ( void ) { return scoreOffset;  } // move to record
	int64_t get_best_score    ( void ) { return bestScore;    } // move to record
	int64_t get_curr_score    ( void ) { return currScore;    } // move to record
	int64_t get_score_dropoff ( void ) { return scoreDropOff; }

	void set_score_offset ( int64_t _scoreOffset ) { scoreOffset = _scoreOffset; } // move to record
	void set_best_score   ( int64_t _bestScore   ) { bestScore   = _bestScore;   } // move to record
	void set_curr_score   ( int64_t _currScore   ) { currScore   = _currScore;   } // move to record

	int8_t get_match_cost    ( void ) { return matchCost;    }
	int8_t get_mismatch_cost ( void ) { return mismatchCost; }
	int8_t get_gap_cost      ( void ) { return gapCost;      }

	vectorType get_vqueryh ( void ) { return vqueryh.simd; } // move to record
	vectorType get_vqueryv ( void ) { return vqueryv.simd; } // move to record

	vectorType get_antiDiag1 ( void ) { return antiDiag1.simd; } // move to record
	vectorType get_antiDiag2 ( void ) { return antiDiag2.simd; } // move to record
	vectorType get_antiDiag3 ( void ) { return antiDiag3.simd; }

	vectorType get_vmatchCost    ( void ) { return vmatchCost;    }
	vectorType get_vmismatchCost ( void ) { return vmismatchCost; }
	vectorType get_vgapCost      ( void ) { return vgapCost;      }
	vectorType get_vzeros        ( void ) { return vzeros;        }

	void update_vqueryh ( uint8_t idx, int8_t value ) { vqueryh.elem[idx] = value; }
	void update_vqueryv ( uint8_t idx, int8_t value ) { vqueryv.elem[idx] = value; }

	void update_antiDiag1 ( uint8_t idx, int8_t value ) { antiDiag1.elem[idx] = value; }
	void update_antiDiag2 ( uint8_t idx, int8_t value ) { antiDiag2.elem[idx] = value; }
	void update_antiDiag3 ( uint8_t idx, int8_t value ) { antiDiag3.elem[idx] = value; }

	void broadcast_antiDiag1 ( int8_t value ) { antiDiag1.simd = setOp( value ); }
	void broadcast_antiDiag2 ( int8_t value ) { antiDiag2.simd = setOp( value ); }
	void broadcast_antiDiag3 ( int8_t value ) { antiDiag3.simd = setOp( value ); }

	void set_antiDiag1 ( vectorType vector ) { antiDiag1.simd = vector; }
	void set_antiDiag2 ( vectorType vector ) { antiDiag2.simd = vector; }
	void set_antiDiag3 ( vectorType vector ) { antiDiag3.simd = vector; }

	void moveRight (void)
	{
		// (a) shift to the left on query horizontal
		vqueryh = shiftLeft( vqueryh.simd );
		vqueryh.elem[LOGICALWIDTH - 1] = queryh[hoffset++];

		// (b) shift left on updated vector 1
		// this places the right-aligned vector 2 as a left-aligned vector 1
		antiDiag1.simd = antiDiag2.simd;
		antiDiag1 = shiftLeft(antiDiag1.simd);
		antiDiag2.simd = antiDiag3.simd;
	}

	void moveDown (void)
	{
		// (a) shift to the right on query vertical
		vqueryv = shiftRight(vqueryv.simd);
		// ==50054==ERROR: AddressSanitizer: heap-buffer-overflow on address 0x60600062b0e0 at pc
		// 0x0001019b50f1 bp 0x70000678ba30 sp 0x70000678ba28 READ of size 1 at 0x60600062b0e0 thread T6
		vqueryv.elem[0] = queryv[voffset++];

		// (b) shift to the right on updated vector 2
		// this places the left-aligned vector 3 as a right-aligned vector 2
		antiDiag1.simd = antiDiag2.simd;
		antiDiag2.simd = antiDiag3.simd;
		antiDiag2 = shiftRight( antiDiag2.simd );
	}

	// Seed position (define starting position and need to be updated when exiting)
	SeedX seed;

	// Sequence Lengths
	unsigned int hlength;
	unsigned int vlength;

	// Sequences as ints
	int8_t* queryh;
	int8_t* queryv;

	// Sequence pointers
	int hoffset;
	int voffset;

	// Constant Scoring Values
	int8_t matchCost;
	int8_t mismatchCost;
	int8_t gapCost;

	// Constant Scoring Vectors
	vectorType vmatchCost;
	vectorType vmismatchCost;
	vectorType vgapCost;
	vectorType vzeros;

	// Computation Vectors
	vectorUnionType antiDiag1;
	vectorUnionType antiDiag2;
	vectorUnionType antiDiag3;

	vectorUnionType vqueryh;
	vectorUnionType vqueryv;

	// X-Drop Variables
	int64_t bestScore;
	int64_t currScore;
	int64_t scoreOffset;
	int64_t scoreDropOff;
	bool xDropCond;
};

void operator+=(XavierState& state1, const XavierState& state2)
{
	state1.bestScore = state1.bestScore + state2.bestScore;
	state1.currScore = state1.currScore + state2.currScore;
}
