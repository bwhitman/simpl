#ifndef RESIDUAL_H
#define RESIDUAL_H

#include "base.h"
#include "peak_detection.h"
#include "partial_tracking.h"
#include "synthesis.h"

extern "C" {
    #include "sms.h"
}

using namespace std;

namespace simpl
{


// ---------------------------------------------------------------------------
// Residual
//
// Calculate a residual signal
// ---------------------------------------------------------------------------

class Residual {
    protected:
        int _frame_size;
        int _hop_size;
        int _sampling_rate;

    public:
        Residual();
        int frame_size();
        void frame_size(int new_frame_size);
        int hop_size();
        virtual void hop_size(int new_hop_size);
        int sampling_rate();
        void sampling_rate(int new_sampling_rate);

        virtual void residual_frame(Frame* frame);
        virtual void find_residual(int synth_size, sample* synth,
                                   int original_size, sample* original,
                                   int residual_size, sample* residual);

        virtual void synth_frame(Frame* frame);
        virtual Frames synth(Frames& frames);
        virtual Frames synth(int original_size, sample* original);
};


// ---------------------------------------------------------------------------
// SMSResidual
// ---------------------------------------------------------------------------
class SMSResidual : public Residual {
    private:
        sample* _temp_synth;
        SMSResidualParams _residual_params;

        SMSPeakDetection _pd;
        SMSPartialTracking _pt;
        SMSSynthesis _synth;

    public:
        SMSResidual();
        ~SMSResidual();
        void hop_size(int new_hop_size);
        int num_stochastic_coeffs();
        void num_stochastic_coeffs(int new_num_stochastic_coeffs);

        // int stochastic_type();
        // void stochastic_type(int new_stochastic_type);

        void residual_frame(Frame* frame);
        void synth_frame(Frame* frame);
};


} // end of namespace Simpl

#endif
