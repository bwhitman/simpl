/* 
 * Copyright (c) 2008 MUSIC TECHNOLOGY GROUP (MTG)
 *                         UNIVERSITAT POMPEU FABRA 
 * 
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of 
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 * 
 */

/*! \file analysis.c
 * \brief main sms analysis routines
 * 
 * the analysis routine here calls all necessary functions to perform the complete
 * SMS analysis, once the desired analysis parameters are set in SMS_AnalParams. 
 */

#include "sms.h"

/*! \brief  maximum size for magnitude spectrum */
#define SMS_MAX_SPEC 8192  

void printAnalysisParams(SMS_AnalParams* params)
{
    printf("fLowestFreq:            %f\n"
           "fHighestFreq:           %f\n"
           "fMinPeakMag:            %f\n"
           "iSamplingRate:          %d\n"
           "iMaxPeaks:              %d\n"
           "fHighestFundamental:    %f\n"
           "iRefHarmonic:           %d\n"
           "fMinRefHarmMag:         %f\n"
           "fRefHarmMagDiffFromMax: %f\n"
           "iSoundType:             %d\n",
            params->fLowestFreq, params->fHighestFreq, params->fMinPeakMag, params->iSamplingRate,
            params->maxPeaks, params->fHighestFundamental, params->iRefHarmonic,
            params->fMinRefHarmMag, params->fRefHarmMagDiffFromMax, params->iSoundType);
}

/*! \brief compute spectrum, find peaks, and fundamental of one frame
 *
 * This is the main core of analysis calls
 *
 * \param iCurrentFrame          frame number to be computed
 * \param pAnalParams     structure of analysis parameters
 * \param fRefFundamental      reference fundamental 
 */
void sms_analyzeFrame(int iCurrentFrame, SMS_AnalParams *pAnalParams, sfloat fRefFundamental)
{
    int i, iFrame;
    SMS_AnalFrame *pCurrentFrame = pAnalParams->ppFrames[iCurrentFrame];
    int iSoundLoc = pCurrentFrame->iFrameSample -((pCurrentFrame->iFrameSize + 1) >> 1) + 1;
    sfloat *pFData = &(pAnalParams->soundBuffer.pFBuffer[iSoundLoc - pAnalParams->soundBuffer.iMarker]);

    /* TODO: this doesn't have to be done every time */
    int sizeWindow =  pCurrentFrame->iFrameSize;
    int sizeMag = sms_power2(sizeWindow);
    sms_getWindow(sizeWindow, pAnalParams->spectrumWindow, pAnalParams->iWindowType);
    sms_scaleWindow(sizeWindow, pAnalParams->spectrumWindow);

    /* compute the magnitude and (zero-windowed) phase spectra */
    sms_spectrum(sizeWindow, pFData, pAnalParams->spectrumWindow, sizeMag,
                 pAnalParams->magSpectrum, pAnalParams->phaseSpectrum,
                 pAnalParams->fftBuffer);

    /* convert magnitude spectra to dB */
    sms_arrayMagToDB(sizeMag, pAnalParams->magSpectrum);

    /* find the prominent peaks */
    pCurrentFrame->nPeaks = sms_detectPeaks(sizeMag,
                                            pAnalParams->magSpectrum,
                                            pAnalParams->phaseSpectrum,
                                            pCurrentFrame->pSpectralPeaks,
                                            pAnalParams);

    /* find a reference harmonic */
    if (pCurrentFrame->nPeaks > 0 &&
        (pAnalParams->iFormat == SMS_FORMAT_H || pAnalParams->iFormat == SMS_FORMAT_HP))
        pCurrentFrame->fFundamental = sms_harmDetection(pAnalParams->nTracks, pCurrentFrame->pSpectralPeaks,
                                                        fRefFundamental, pAnalParams->iRefHarmonic,
                                                        pAnalParams->fLowestFundamental, pAnalParams->fHighestFundamental,
                                                        pAnalParams->iSoundType, pAnalParams->fMinRefHarmMag,
                                                        pAnalParams->fRefHarmMagDiffFromMax);
}

/*! \brief re-analyze the previous frames if necessary
 *
 * \todo explain when this is necessary
 *
 * \param iCurrentFrame             current frame number
 * \param pAnalParams              structure with analysis parameters
 * \return 1 if frames are good, -1 if analysis is necessary
 * \todo is the return value info correct? Why isn't it used in sms_analyze?
 */
static int ReAnalyzeFrame(int iCurrentFrame, SMS_AnalParams *pAnalParams)
{
    sfloat fFund, fLastFund, fDev;
    int iNewFrameSize, i;
    sfloat fAvgDeviation = sms_fundDeviation(pAnalParams, iCurrentFrame);
    int iFirstFrame = iCurrentFrame - pAnalParams->minGoodFrames;

    /*! \todo make this a < 0 check, but first make sure sms_fundDeviation does not
        return values below zero */
    if(fAvgDeviation == -1)
        return -1;

    /* if the last SMS_MIN_GOOD_FRAMES are stable look before them */
    /*  and recompute the frames that are not stable */
    if (fAvgDeviation <= pAnalParams->maxDeviation)
    {
        for(i = 0; i < pAnalParams->analDelay; i++)
        {
            if(pAnalParams->ppFrames[iFirstFrame - i]->iFrameNum <= 0 ||
               pAnalParams->ppFrames[iFirstFrame - i]->iStatus == SMS_FRAME_RECOMPUTED)
                return -1;
            fFund = pAnalParams->ppFrames[iFirstFrame - i]->fFundamental;
            fLastFund = pAnalParams->ppFrames[iFirstFrame - i + 1]->fFundamental;
            fDev = fabs (fFund - fLastFund) / fLastFund;
            iNewFrameSize = ((pAnalParams->iSamplingRate / fLastFund) *
                            pAnalParams->fSizeWindow/2) * 2 + 1;

            if(fFund <= 0 || fDev > .2 ||
               fabs((double)(pAnalParams->ppFrames[iFirstFrame - i]->iFrameSize -
                             iNewFrameSize)) / iNewFrameSize >= .2)
            {
                pAnalParams->ppFrames[iFirstFrame - i]->iFrameSize = iNewFrameSize;
                pAnalParams->ppFrames[iFirstFrame - i]->iStatus = SMS_FRAME_READY;

                /* recompute frame */
                sms_analyzeFrame(iFirstFrame - i, pAnalParams, fLastFund);
                pAnalParams->ppFrames[iFirstFrame - i]->iStatus = SMS_FRAME_RECOMPUTED;

                if(fabs(pAnalParams->ppFrames[iFirstFrame - i]->fFundamental - fLastFund) /
                        fLastFund >= .2)
                    return -1;
            }
        }
    }
    return 1;
}

int sms_findPeaks(int sizeWaveform, sfloat *pWaveform, SMS_AnalParams *pAnalParams, SMS_SpectralPeaks *pSpectralPeaks)
{
    int iCurrentFrame = pAnalParams->iMaxDelayFrames - 1;  /* frame # of current frame */
    sfloat fRefFundamental = 0;   /* reference fundamental for current frame */
    int i, iError, iExtraSamples; /* samples used for next analysis frame */
    SMS_AnalFrame *pTmpAnalFrame;

    /* set initial analysis-window size */
    if(pAnalParams->windowSize == 0)
        pAnalParams->windowSize = pAnalParams->iDefaultSizeWindow;

    /* fill sound buffer and perform pre-emphasis */
    if (sizeWaveform > 0)
        sms_fillSoundBuffer(sizeWaveform, pWaveform, pAnalParams);

    /* move analysis data one frame back */
    pTmpAnalFrame = pAnalParams->ppFrames[0];
    for(i = 1; i < pAnalParams->iMaxDelayFrames; i++)
        pAnalParams->ppFrames[i-1] = pAnalParams->ppFrames[i];
    pAnalParams->ppFrames[pAnalParams->iMaxDelayFrames-1] = pTmpAnalFrame;

    /* initialize the current frame */
    sms_initFrame(iCurrentFrame, pAnalParams, pAnalParams->windowSize);
    if(sms_errorCheck())
    {
        printf("Error in init frame: %s \n", sms_errorString());
        return(0);
    }

    if(pAnalParams->ppFrames[iCurrentFrame]->iStatus == SMS_FRAME_READY)
    {
        sfloat fAvgDev = sms_fundDeviation(pAnalParams, iCurrentFrame - 1);

        /* if single note use the default fundamental as reference */
        if(pAnalParams->iSoundType == SMS_SOUND_TYPE_NOTE)
            fRefFundamental = pAnalParams->fDefaultFundamental;
        /* if sound is stable use the last fundamental as a reference */
        else if(fAvgDev != -1 && fAvgDev <= pAnalParams->maxDeviation)
            fRefFundamental = pAnalParams->ppFrames[iCurrentFrame - 1]->fFundamental;
        else
            fRefFundamental = 0;

        /* compute spectrum, find peaks, and find fundamental of frame */
        sms_analyzeFrame(iCurrentFrame, pAnalParams, fRefFundamental);

        /* set the size of the next analysis window */
        if(pAnalParams->ppFrames[iCurrentFrame]->fFundamental > 0 &&
            pAnalParams->iSoundType != SMS_SOUND_TYPE_NOTE)
            pAnalParams->windowSize = sms_sizeNextWindow(iCurrentFrame, pAnalParams);

        /* figure out how much needs to be read next time */
        // how many processed - sample no. of end of next frame
        // = no. samples that we haven't processed yet from whenever, if sizeNextRead was 0
        iExtraSamples = (pAnalParams->soundBuffer.iMarker + pAnalParams->soundBuffer.sizeBuffer) -
                        (pAnalParams->ppFrames[iCurrentFrame]->iFrameSample + pAnalParams->sizeHop);

        pAnalParams->sizeNextRead = MAX(0, (pAnalParams->windowSize+1)/2 - iExtraSamples);
        ReAnalyzeFrame(iCurrentFrame, pAnalParams);

        /* save peaks */
        pSpectralPeaks->nPeaksFound = pAnalParams->ppFrames[iCurrentFrame]->nPeaks;
        pSpectralPeaks->nPeaks = pAnalParams->maxPeaks;
        pSpectralPeaks->pSpectralPeaks = pAnalParams->ppFrames[iCurrentFrame]->pSpectralPeaks;

        /* convert peak amps to linear */
        for(i = 0; i < pSpectralPeaks->nPeaksFound; i++)
        {
            pSpectralPeaks->pSpectralPeaks[i].fMag = pow(10.0, 0.05*(pSpectralPeaks->pSpectralPeaks[i].fMag));
        }
        return pSpectralPeaks->nPeaks;
    }
    else
    {
        return 0;
    }
}

void sms_setPeaks(SMS_AnalParams *pAnalParams, int numamps, sfloat* amps,
                  int numfreqs, sfloat* freqs, int numphases, sfloat* phases)
{
    int i;
    SMS_AnalFrame *tempFrame;

    /* move analysis data one frame back */
    tempFrame = pAnalParams->ppFrames[0];
    for(i = 1; i < pAnalParams->iMaxDelayFrames; i++)
        pAnalParams->ppFrames[i-1] = pAnalParams->ppFrames[i];
    pAnalParams->ppFrames[pAnalParams->iMaxDelayFrames-1] = tempFrame;

    /* initialize the current frame */
    SMS_AnalFrame *currentFrame = pAnalParams->ppFrames[2];
    sms_initFrame(2, pAnalParams, 0);
    if(sms_errorCheck())
    {
        printf("Error in init frame: %s \n", sms_errorString());
        return;
    }

    for(i = 0; i < numamps; i++)
    {
        /* copy current peaks data */
        currentFrame->pSpectralPeaks[i].fMag = sms_magToDB(amps[i]);
        currentFrame->pSpectralPeaks[i].fFreq = freqs[i];
        currentFrame->pSpectralPeaks[i].fPhase = phases[i];
    }
    currentFrame->nPeaks = numamps;
    currentFrame->iStatus = SMS_FRAME_READY;

    /* harmonic detection */
    if (currentFrame->nPeaks > 0 &&
        (pAnalParams->iFormat == SMS_FORMAT_H || pAnalParams->iFormat == SMS_FORMAT_HP))
    {
        /* get a reference fundamental */
        sfloat refFundamental = 0;
        sfloat avgDeviation = sms_fundDeviation(pAnalParams, 1);
        if(pAnalParams->iSoundType == SMS_SOUND_TYPE_NOTE)
            refFundamental = pAnalParams->fDefaultFundamental;
        /* if sound is stable use the last fundamental as a reference */
        else if(avgDeviation != -1 && avgDeviation <= pAnalParams->maxDeviation)
            refFundamental = pAnalParams->ppFrames[1]->fFundamental;
        else
            refFundamental = 0;

        currentFrame->fFundamental = sms_harmDetection(pAnalParams->nTracks, currentFrame->pSpectralPeaks,
                                                       refFundamental, pAnalParams->iRefHarmonic,
                                                       pAnalParams->fLowestFundamental, pAnalParams->fHighestFundamental,
                                                       pAnalParams->iSoundType, pAnalParams->fMinRefHarmMag,
                                                       pAnalParams->fRefHarmMagDiffFromMax);
    }
}

int sms_findPartials(SMS_Data *pSmsData, SMS_AnalParams *pAnalParams)
{
    /* clear SMS output */
    sms_clearFrame(pSmsData);

    /* incorporate the peaks into the corresponding tracks */
    /* todo: allow for longer analysis delays */
    if(pAnalParams->ppFrames[1]->fFundamental > 0 ||
       ((pAnalParams->iFormat == SMS_FORMAT_IH || pAnalParams->iFormat == SMS_FORMAT_IHP) &&
        pAnalParams->ppFrames[1]->nPeaks > 0))
    {
        sms_peakContinuation(1, pAnalParams);
    }

    /* fill gaps and delete short tracks */
    /* todo: allow for longer analysis delays */
    if(pAnalParams->iCleanTracks > 0)
    {
        sms_cleanTracks(1, pAnalParams);
    }

    /* output data */
    sms_allocFrame(pSmsData, pAnalParams->nTracks, pAnalParams->nStochasticCoeff,
                   1, pAnalParams->iStochasticType, pAnalParams->specEnvParams.nCoeff);

    int length = sizeof(sfloat) * pSmsData->nTracks;
    memcpy((char *) pSmsData->pFSinFreq, (char *)
           pAnalParams->ppFrames[0]->deterministic.pFSinFreq, length);
    memcpy((char *) pSmsData->pFSinAmp, (char *)
           pAnalParams->ppFrames[0]->deterministic.pFSinAmp, length);

    /* convert mags back to linear */
    sms_arrayDBToMag(pSmsData->nTracks, pSmsData->pFSinAmp);

    if(pAnalParams->iFormat == SMS_FORMAT_HP ||
       pAnalParams->iFormat == SMS_FORMAT_IHP)
        memcpy((char *) pSmsData->pFSinPha, (char *)
               pAnalParams->ppFrames[0]->deterministic.pFSinPha, length);

    /* do post-processing (for now, spectral envelope calculation and storage) */
    if(pAnalParams->specEnvParams.iType != SMS_ENV_NONE)
    {
        sms_spectralEnvelope(pSmsData, &pAnalParams->specEnvParams);
    }

    return 1;
}

int sms_findResidual(int sizeSynthesis, sfloat* pSynthesis,
                     int sizeOriginal, sfloat* pOriginal,
                     int sizeResidual, sfloat* pResidual,
                     SMS_AnalParams *analParams)
{
    if(sizeSynthesis != sizeOriginal || sizeOriginal != sizeResidual)
    {
        return -1;
    }

    /* perform preemphasis */
    int i;
    for(i = 0; i < sizeSynthesis; i++)
    {
        pOriginal[i] = sms_preEmphasis(pOriginal[i], analParams);
    }

    /*sms_residual(sizeResidual, pSynthesis, pOriginal, pResidual);*/
    return 0;
}

void sms_approxResidual(int sizeResidual, sfloat* pResidual,
                        SMS_Data* pSmsData, SMS_SynthParams* pSynthParams)
{
    /* perform stochastic analysis after 1 frame of the     */
    /* deterministic synthesis because it needs two frames  */
//  if (pAnalParams->ppFrames[0]->iStatus != SMS_FRAME_EMPTY &&
//      pAnalParams->ppFrames[0]->iStatus != SMS_FRAME_END)
//  {
//      int sizeResidual = pAnalParams->sizeHop * 2;
//      int iSoundLoc = pAnalParams->ppFrames[0]->iFrameSample - pAnalParams->sizeHop;
//  //          sfloat *pOriginal = &(pAnalParams->soundBuffer.pFBuffer[iSoundLoc - pAnalParams->soundBuffer.iMarker]);
//      sfloat *pOriginal;
//      sfloat *pFResidual;
//
//      static sfloat *pWindow;
//      static int sizeWindowArray = 0;
//
//  //          int sizeData = MIN(pAnalParams->soundBuffer.sizeBuffer -
//  //                            (iSoundLoc - pAnalParams->soundBuffer.iMarker),
//  //                            sizeResidual);
//      int sizeData = sizeResidual;
//      if ((pFResidual = (sfloat *) calloc (sizeResidual, sizeof(sfloat))) == NULL)
//      {
//          sms_error("sms_analyze: error allocating memory for pFResidual");
//          return -1;
//      }
//      if (sizeWindowArray != sizeData)
//      {
//          if(sizeWindowArray != 0) free(pWindow);
//          if((pWindow = (sfloat *) calloc(sizeData, sizeof(sfloat))) == NULL)
//          {
//                  sms_error("sms_analyze: error allocating memory for pWindow");
//                  return -1;
//          }
//          sms_getWindow( sizeData, pWindow, SMS_WIN_HAMMING);
//          sms_scaleWindow( sizeData, pWindow);
//          sizeWindowArray = sizeData;
//      }
//
//      /* obtain residual sound from original and synthesized sounds.  accumulate the residual percentage.*/
//      pAnalParams->fResidualAccumPerc += sms_residual(sizeData,
//                                                      pAnalParams->synthBuffer.pFBuffer,
//                                                      pOriginal,
//                                                      pFResidual,
//                                                      pWindow);
//
//      if (pAnalParams->iStochasticType == SMS_STOC_APPROX)
//      {
//          /* filter residual with a high pass filter (it solves some problems) */
//          sms_filterHighPass (sizeData, pFResidual, pAnalParams->iSamplingRate);
//
//          /* approximate residual */
//          sms_stocAnalysis (sizeData, pFResidual, pWindow, pSmsData);
//      }
//      else if  (pAnalParams->iStochasticType == SMS_STOC_IFFT)
//      {
//          int sizeMag = sms_power2(sizeData >> 1);
//          sms_spectrum (sizeData, pFResidual, pWindow, sizeMag, pSmsData->pFStocCoeff,
//                  pSmsData->pResPhase);
//      }
//
//      /* get sharper transitions in deterministic representation */
//  //              sms_scaleDet (pAnalParams->synthBuffer.pFBuffer, pOriginal,
//  //                            pAnalParams->ppFrames[0]->deterministic.pFSinAmp,
//  //                            pAnalParams, pSmsData->nTracks);
//
//      pAnalParams->ppFrames[0]->iStatus = SMS_FRAME_DONE;
//
//      free ((char *) pFResidual); /* \todo get rid of this free, manage memory the same as spectrum functions */
//  }
}

int sms_analyze(int sizeWaveform, sfloat *pWaveform, SMS_Data *pSmsData, SMS_AnalParams *pAnalParams)
{
    int iCurrentFrame = pAnalParams->iMaxDelayFrames - 1;  /* frame # of current frame */
    int delayFrames; 
    int i, iError, iExtraSamples;              /* samples used for next analysis frame */
    sfloat fRefFundamental = 0;   /* reference fundamental for current frame */
    SMS_AnalFrame *pTmpAnalFrame;

    /* set the frame delay, checking that it does not exceed the given maximum
     *
     * TODO: check for good values of pAnalParams->minGoodFrames and
     * pAnalParams->analDelay here too? Or figure out why sms_crashes if
     * pAnalParamx->iMaxDelayFrames is changed without changing the other
     * two variables.
     */
    delayFrames = pAnalParams->minGoodFrames + pAnalParams->analDelay;
    if(delayFrames > (pAnalParams->iMaxDelayFrames - 1))
        delayFrames = pAnalParams->iMaxDelayFrames - 1;

    /* clear SMS output */
    sms_clearFrame(pSmsData);

    /* set initial analysis-window size */
    if(pAnalParams->windowSize == 0)
        pAnalParams->windowSize = pAnalParams->iDefaultSizeWindow;

    /* fill the input sound buffer and perform pre-emphasis */
    if(sizeWaveform > 0)
        sms_fillSoundBuffer(sizeWaveform, pWaveform, pAnalParams);

    /* move analysis data one frame back */
    pTmpAnalFrame = pAnalParams->ppFrames[0];
    for(i = 1; i < pAnalParams->iMaxDelayFrames; i++)
        pAnalParams->ppFrames[i-1] = pAnalParams->ppFrames[i];
    pAnalParams->ppFrames[pAnalParams->iMaxDelayFrames-1] = pTmpAnalFrame;

    /* initialize the current frame */
    sms_initFrame(iCurrentFrame, pAnalParams, pAnalParams->windowSize);
    if(sms_errorCheck())
    {
        printf("error in init frame: %s \n", sms_errorString());
        return -1;
    }

    /* if right data in the sound buffer do analysis */
    if(pAnalParams->ppFrames[iCurrentFrame]->iStatus == SMS_FRAME_READY)
    {
        sfloat fAvgDev = sms_fundDeviation(pAnalParams, iCurrentFrame - 1);

        /* if single note use the default fundamental as reference */
        if(pAnalParams->iSoundType == SMS_SOUND_TYPE_NOTE)
            fRefFundamental = pAnalParams->fDefaultFundamental;
        /* if sound is stable use the last fundamental as a reference */
        else if(fAvgDev != -1 && fAvgDev <= pAnalParams->maxDeviation)
            fRefFundamental = pAnalParams->ppFrames[iCurrentFrame - 1]->fFundamental;
        else
            fRefFundamental = 0;

        /* compute spectrum, find peaks, and find fundamental of frame */
        sms_analyzeFrame(iCurrentFrame, pAnalParams, fRefFundamental);

        /* set the size of the next analysis window */
        if(pAnalParams->ppFrames[iCurrentFrame]->fFundamental > 0 &&
           pAnalParams->iSoundType != SMS_SOUND_TYPE_NOTE)
            pAnalParams->windowSize = sms_sizeNextWindow (iCurrentFrame, pAnalParams);

        /* figure out how much needs to be read next time */
        iExtraSamples =
            (pAnalParams->soundBuffer.iMarker + pAnalParams->soundBuffer.sizeBuffer) -
            (pAnalParams->ppFrames[iCurrentFrame]->iFrameSample + pAnalParams->sizeHop);

        pAnalParams->sizeNextRead = MAX(0, (pAnalParams->windowSize+1)/2 - iExtraSamples);

        /* check again the previous frames and recompute if necessary */
        ReAnalyzeFrame(iCurrentFrame, pAnalParams);
    }

    /* incorporate the peaks into the corresponding tracks */
    /* This is done after a pAnalParams->iMaxDelayFrames delay  */
    if(pAnalParams->ppFrames[iCurrentFrame - delayFrames]->fFundamental > 0 ||
       ((pAnalParams->iFormat == SMS_FORMAT_IH || pAnalParams->iFormat == SMS_FORMAT_IHP) &&
         pAnalParams->ppFrames[iCurrentFrame - delayFrames]->nPeaks > 0))
        sms_peakContinuation(iCurrentFrame - delayFrames, pAnalParams);

    /* fill gaps and delete short tracks */
    if(pAnalParams->iCleanTracks > 0 &&
       pAnalParams->ppFrames[iCurrentFrame - delayFrames]->iStatus != SMS_FRAME_EMPTY)
        sms_cleanTracks(iCurrentFrame - delayFrames, pAnalParams);

    /* do stochastic analysis */
    if(pAnalParams->iStochasticType != SMS_STOC_NONE)
    {
        /* synthesize deterministic signal */
        if(pAnalParams->ppFrames[1]->iStatus != SMS_FRAME_EMPTY &&
           pAnalParams->ppFrames[1]->iStatus != SMS_FRAME_END)
        {
            /* shift synthesis buffer */
            memcpy(pAnalParams->synthBuffer.pFBuffer,
                   pAnalParams->synthBuffer.pFBuffer+pAnalParams->sizeHop,
                   sizeof(sfloat) * pAnalParams->sizeHop);
            memset(pAnalParams->synthBuffer.pFBuffer+pAnalParams->sizeHop,
                   0, sizeof(sfloat) * pAnalParams->sizeHop);

            /* get deterministic signal with phase  */
            sms_sineSynthFrame(&pAnalParams->ppFrames[1]->deterministic,
                               pAnalParams->synthBuffer.pFBuffer+pAnalParams->sizeHop,
                               pAnalParams->sizeHop, &pAnalParams->prevFrame,
                               pAnalParams->iSamplingRate);
        }

        /* perform stochastic analysis after 1 frame of the     */
        /* deterministic synthesis because it needs two frames  */
        if(pAnalParams->ppFrames[0]->iStatus != SMS_FRAME_EMPTY &&
           pAnalParams->ppFrames[0]->iStatus != SMS_FRAME_END)
        {
            int sizeResidual = pAnalParams->sizeHop * 2;
            int iSoundLoc = pAnalParams->ppFrames[0]->iFrameSample - pAnalParams->sizeHop;
            sfloat *pOriginal = &(pAnalParams->soundBuffer.pFBuffer[iSoundLoc -
                                  pAnalParams->soundBuffer.iMarker]);

            int sizeData = MIN(pAnalParams->soundBuffer.sizeBuffer -
                               (iSoundLoc - pAnalParams->soundBuffer.iMarker),
                               pAnalParams->sizeResidual);

            if(sizeData > pAnalParams->sizeResidual)
            {
                sms_error("Residual size larger than expected.");
                return -1;
            }
            else if(sizeData < pAnalParams->sizeResidual)
            {
                /* should only happen if we're at the end of a sound, unless hop size changes */
                sms_getWindow(sizeData, pAnalParams->residualWindow, SMS_WIN_HAMMING);
                sms_scaleWindow(sizeData, pAnalParams->residualWindow);
            }

            /* obtain residual sound from original and synthesized sounds.  accumulate the residual percentage.*/
            pAnalParams->fResidualAccumPerc += sms_residual(sizeData,
                                                            pAnalParams->synthBuffer.pFBuffer,
                                                            pOriginal,
                                                            pAnalParams->residual,
                                                            pAnalParams->residualWindow);

            if(pAnalParams->iStochasticType == SMS_STOC_APPROX)
            {
                /* filter residual with a high pass filter (it solves some problems) */
                sms_filterHighPass(sizeData, pAnalParams->residual, pAnalParams->iSamplingRate);

                /* approximate residual */
                sms_stocAnalysis(sizeData, pAnalParams->residual, pAnalParams->residualWindow, 
                                 pSmsData, pAnalParams);
            }
            else if(pAnalParams->iStochasticType == SMS_STOC_IFFT)
            {
                int sizeMag = sms_power2(sizeData >> 1);
                sms_spectrum(sizeData, pAnalParams->residual, pAnalParams->residualWindow, 
                             sizeMag, pSmsData->pFStocCoeff, pSmsData->pResPhase,
                             pAnalParams->fftBuffer);
            }

            /* get sharper transitions in deterministic representation */
            sms_scaleDet(pAnalParams->synthBuffer.pFBuffer, pOriginal,
                         pAnalParams->ppFrames[0]->deterministic.pFSinAmp,
                         pAnalParams, pSmsData->nTracks);

            pAnalParams->ppFrames[0]->iStatus = SMS_FRAME_DONE;
        }
    }
    else if(pAnalParams->ppFrames[0]->iStatus != SMS_FRAME_EMPTY &&
            pAnalParams->ppFrames[0]->iStatus != SMS_FRAME_END)
        pAnalParams->ppFrames[0]->iStatus = SMS_FRAME_DONE;

    /* get the result */
    if(pAnalParams->ppFrames[0]->iStatus == SMS_FRAME_EMPTY)
    {
        /* no partials yet, so output the current peaks for testing */
        int numPeaks = pAnalParams->ppFrames[iCurrentFrame]->nPeaks;
        int numTracks = pSmsData->nTracks;
        numTracks = MIN(numPeaks, numTracks);
        for(i = 0; i < numTracks; i++)
        {
            pSmsData->pFSinFreq[i] = pAnalParams->ppFrames[iCurrentFrame]->pSpectralPeaks[i].fFreq;
            pSmsData->pFSinAmp[i] = pAnalParams->ppFrames[iCurrentFrame]->pSpectralPeaks[i].fMag;
            if(pAnalParams->iFormat == SMS_FORMAT_HP ||
               pAnalParams->iFormat == SMS_FORMAT_IHP)
            {
                pSmsData->pFSinPha[i] = pAnalParams->ppFrames[iCurrentFrame]->pSpectralPeaks[i].fPhase;
            }
        }
        pSmsData->nTracks = numTracks;
        return 0;
    }
    /* return analysis data */
    else if(pAnalParams->ppFrames[0]->iStatus == SMS_FRAME_DONE)
    {
        /* put data into output */
        int length = sizeof(sfloat) * pSmsData->nTracks;
        memcpy((char *) pSmsData->pFSinFreq, (char *)
               pAnalParams->ppFrames[0]->deterministic.pFSinFreq, length);
        memcpy((char *) pSmsData->pFSinAmp, (char *)
               pAnalParams->ppFrames[0]->deterministic.pFSinAmp, length);

        /* convert mags back to linear */
        sms_arrayDBToMag(pSmsData->nTracks, pSmsData->pFSinAmp);
        if(pAnalParams->iFormat == SMS_FORMAT_HP ||
           pAnalParams->iFormat == SMS_FORMAT_IHP)
            memcpy((char *) pSmsData->pFSinPha, (char *)
                   pAnalParams->ppFrames[0]->deterministic.pFSinPha, length);

        /* do post-processing (for now, spectral envelope calculation and storage) */
        if(pAnalParams->specEnvParams.iType != SMS_ENV_NONE)
        {
            sms_spectralEnvelope(pSmsData, &pAnalParams->specEnvParams);
        }
        return 1;
    }
    /* done, end of sound */
    else if(pAnalParams->ppFrames[0]->iStatus == SMS_FRAME_END)
        return -1;
    else
    {
        sms_error("sms_analyze error: wrong status of frame.");
        return -1;
    }
    return 1;
}
