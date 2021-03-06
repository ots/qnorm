/******************************************************************************
* FILE: pQnorm.c
* DESCRIPTION:
*   parallel version
*   Improved sequential prototype for Qnorm 
*   Qnormalisation Method: function that implements the ben Bolstad Method
*   quantile Normalization of High density Oliglonucleotide Array Data
*
* AUTHOR: O.Trelles (23 Feb.09)
* 23.Feb.09  : Using command line argums
*              Qnorm [-o=Value]  see below Command line params
*
*              fList  : contains a list of nExp filenames (gene-expression data files) 
*                          line format: fileName[TAB]nGenes[TAB]FileType[NEWLINE]
*              nRows  : number of genes in each file 
*              Normalised.fname where the normalised values will be stored 
*                       (as a text-tabulated matrix)
*              mode : m: keep the Index matrix in memory ( d: in disk)
*                       THIS VERSION REQUIRES ALL FILES WITH THE SAME NUMBER OF GENES
*
*
*  Command line Parameters
*         sintaxis:    Qnorm [-Option=value]... 
*
*  Option  Description              Default value     alternative Values
*  ------  -----------------------  -------------     ------------------
*  -p      Number of Processors        4               Only in parallel version
*  -i      File name (list of files)   qInput.txt      valid existing pathname
*  -o      Output binary matrix        qOut.bin        binary by columns file
*  -e      Number of experiments       2               positive integer
*  -g      NUmber of genes             15              positive integer
*  -t      Traspose the fileOut        Not             -T (yes)
*  -M      Index Matrix in mem         D (in disk)     -M (in memory)
*  -V      Verbose mode                Not             -V  
* ---------------------------------------------------------------------------
*
*	@returns >0 if everything was fine <0 if there was an error
*
* LAST REVISED: 27/02/09
******************************************************************************/
#include <omp.h>
#include "Qfunc.c"

int main(int ac, char **av){

        struct Files *fList=NULL;
        struct params *p=NULL;
fprintf(stderr, "P1\n");fflush(stderr);
        p = CommandLine(ac,av);
fprintf(stderr, "P2\n");fflush(stderr);

	if ((fList=LoadListOfFiles(p))==NULL) 
           terror("Loading list of files");
fprintf(stderr, "P3\n");fflush(stderr);
/*
        QNormMain(p,fList);	

	
	return 1;
}


void QNormMain(struct params *p, struct Files* fList){
*/
   double *dataIn, *dataOut;
   int **mIndex;
   int *dIndex;
   struct Average *AvG; // global Average by row
   int i,j,k;
   FILE *fI, *fOut;
   int nG=p->nG;
   int nE=p->nE;
   int nP, tid;
   int From, To, Range; 

fprintf(stderr, "P4\n");fflush(stderr);
#pragma omp parallel shared(nG, nE,nP,mIndex,fI,fOut, fList, p) private(i,j,k,tid,dataIn, dataOut, dIndex, AvG, From, To, Range)
 {
    tid = omp_get_thread_num();
    if (tid == 0) {
       nP = omp_get_num_threads();
       if (p->Verbose) fprintf(stderr,"Number of threads = %d\n", nP);
    }
    if (p->Verbose) fprintf(stderr,"Thread %d starting...\n",tid);


fprintf(stderr, "P5\n");fflush(stderr);
   // Memory===========================================
   // Index array 
   if (p->MemIndex) { // in memory - full
     if (tid==0) { // Only Master process------ 
       if ((mIndex=(int **)calloc(nG,sizeof(int*)))==NULL) 
          terror("memory for index1");
       for (i=0; i<nG;i++)
         if((mIndex[i]=(int *)calloc(nE,sizeof(int)))==NULL) 
           terror("memory for index2 full matrix");
      } // end tid==0
     } else 
     // OjO: all threads open the file??
     if ((fI=fopen("~tmp","wb"))==NULL) terror("opening tmp-index file");

   // This will always be necessary to decuple the function
   if((dIndex=(int *)calloc(nG,sizeof(int)))==NULL) terror("memory for index2");

   if ((dataIn=(double *)calloc(nG,sizeof(double)))==NULL) terror("memory for dataIn array");
   if ((AvG   =(struct Average *)calloc(nG,sizeof(struct Average)))==NULL) terror("memory for Average array");

   for (j=0; j< nG;j++) { // init Accumulation array 
      AvG[j].Av=0;        // =HUGE_VAL; ???
      AvG[j].num=0;
   }

    // Parallel section [1]-----------------
  #pragma omp sections nowait
  {
    #pragma omp section
    {

   // QNORM ===============================================================
   if (p->Verbose) fprintf(stderr,"[1st-Step]");

   // LOAD DISTRIBUTION------------------
   Range = nE / nP;

   From = tid * Range;
   To   = (tid+1)*Range;
   if (To > nE) To = nE;

   for (i=From; i< To; i++) { // Qnorm for each datafile: STEP 1

        LoadFile(fList, i, dataIn);

        if (p->Verbose) { fprintf(stderr,"."); fflush(stderr);}

#ifdef DEBUG
        DebugPrint("Load", dataIn, fList[i].nG); 
#endif
        Qnorm1(dataIn, dIndex, fList[i].nG); // dataIn returns ordered and Index contains the origial position

#ifdef DEBUG
        DebugPrint("Sorted", dataIn, nG);         
#endif
        
        AccumulateRow(AvG, dataIn , nG);

        // now decide how to proceed with indexes
        if (p->MemIndex) { // in memory - full
          for (j=0;j<nG;j++) 
            mIndex[j][i]= dIndex[j];
        } else {         // in disk
          fseek(fI, nG*i*sizeof(int), SEEK_SET);
          fwrite(dIndex, sizeof(int), nG, fI);
        }


#ifdef DEBUG
        fprintf(stderr,"Index (col=%d)\n",i);
        for (j=0;j<nG;j++) fprintf (stderr,"%d ", dIndex[j]); fprintf(stderr,"\n");
#endif


   }

   } // End Parallel Section [1]----------
  } // End nopwait section

   // Row average  ----------------------------------------------
   for (i=0;i<nG;i++) 
       AvG[i].Av /=AvG[i].num;

/*      if ((__finite(AvG[i].Av))&&(!__isnan(AvG[i].Av)))
          AvG[i].Av /=AvG[i].num;
      else AvG[i].Av =0;
*/

#ifdef DEBUG
     fprintf(stderr, "Row Average------------\n");
     for (j=0;j<nG;j++) fprintf (stderr,"%f (%d) ", AvG[j].Av,AvG[j].num); 
     fprintf(stderr,"\n");
#endif


   // Finally produce the ORDERED output file [STEP 2]-------------------------

   if (!p->MemIndex) { 
      fclose(fI);
      if ((fI=fopen("~tmp","rb"))==NULL) terror("opening tmp-index for reading file");
   }

   // oputput file (by cols)
   if ((fOut=fopen(p->fOutName,"wb"))==NULL) terror("opening OUTPUT file");

   if ((dataOut=(double *)calloc(nG,sizeof(double)))==NULL) terror("memory for dataOut array");
   if (p->Verbose) { fprintf(stderr,"\n[2nd Step]"); fflush(stderr);}

   for (i=0;i<nE;i++) {
        if (p->Verbose) {fprintf(stderr,"."); fflush(stderr);}

        if (p->MemIndex) { // in memory - full
          for (j=0;j<nG;j++) 
            dIndex[j]=mIndex[j][i];
        } else {
          fseek(fI, nG*i*sizeof(int), SEEK_SET);
          fread(dIndex, sizeof(int), nG, fI);
        }

#ifdef DEBUG
        fprintf(stderr,"recovered Index (col=%d)\n",i);
        for (j=0;j<nG;j++) fprintf (stderr,"%d ", dIndex[j]); fprintf(stderr,"\n");
#endif


        // complete the output vector
        for (j=0;j<nG;j++) 
          dataOut[dIndex[j]]=AvG[j].Av; // OJO

#ifdef DEBUG
        fprintf(stderr,"[pos=%ld] ",(long)nG*i*sizeof(double));
        DebugPrint("Out to write", dataOut, nG); 
#endif

        fseek(fOut, (long)nG*i*sizeof(double), SEEK_SET);
        fwrite(dataOut, sizeof(double), nG, fOut);
           
   }
   fclose(fOut);
   if (!p->MemIndex) fclose(fI);

   if (p->Traspose){
    if (p->Verbose) 
       { fprintf(stderr,"\nTransposing Disk->Disk"); fflush(stderr); }
    TransposeBin2Txt(p);
   }

   if (p->Verbose) fprintf(stderr,"Thread %d done.\n",tid);   
 } // end of the parallel section

   return 1;

}


// input returns ordered and Index contains the origial position

int Qnorm1(double *input, int *dIndex, int nG){
	int i,j,k,n;

	for (j=0; j<nG;j++) dIndex[j]=j; // init the indexes array

/*
	for (j=0; j<nG;j++) // UNIFY NAN CONSTANT 
	   if ((!__finite(input[j]))||(__isnan(input[j]))) input[j]=HUGE_VAL;
*/

	QsortC(input,0,nG-1,dIndex); // Quicksort 

        return 1;
}

void AccumulateRow(struct Average *AvG, double *input , int nG){
        int i;
 	
        for (i=0;i<nG;i++) {

/*
           if ((__finite(input[i])&&(!__isnan(input[i])))){
		if ((!__finite(AvG[i].Av))||(__isnan(AvG[i].Av))){
			AvG[i].Av=0;
		}
*/
		AvG[i].Av+=input[i];
		AvG[i].num++;
/*           }
*/	}


	return;

}

