% (1) Generate Object
% sampleName: the type and name to specify a sample object, not case-sensitive
% Ny*Nx:    Sample object size
% WGT:      Sample object ground truth
sampleName = 'Phantom'; % choose from {'Phantom', 'Brain', ''Golosio', 'circle', 'checkboard', 'fakeRod'}; not case-sensitive
Nx = 50; Ny = Nx; WSz = [Ny, Nx]; %  XH: Ny = 100 -> 10 or 50 for debug.  currently assume object shape is square not general rectangle
WGT = createObject(sampleName, WSz);

% (2) Scan Object: Create Forward Model and Sinograms
% NTheta*NTau:  Sinogram Size
% L:            Forward Model, sparse matrix of NTheta*NTau by Ny*Nx
% SAll:         Sinogram for all scans of NTheta*NTau*NScan, SAll(:,:,n) for the nth scan
NTheta = 20;  % sample angle #. Use odd NOT even, for display purpose of sinagram of Phantom. As even NTheta will include theta of 90 degree where sinagram will be very bright as Phantom sample has verical bright line on left and right boundary.
NTau = ceil(sqrt(sum(WSz.^2))); NTau = NTau + rem(NTau-Ny,2); % number of discrete beam, enough to cover object diagonal, also use + rem(NTau-Ny,2) to make NTau the same odd/even as Ny just for perfection, so that for theta=0, we have sum(WGT, 2)' and  S(1, (1:Ny)+(NTau-Ny)/2) are the same with a scale factor
SSz = [NTheta, NTau];
L = XTM_Tensor_XH(WSz, NTheta, NTau);
S = reshape(L*WGT(:), NTheta, NTau);
%%
  A = [0.81  0.91  0.28  0.96  0.96
       0.91  0.63  0.55  0.16  0.49
       0.13  0.10  0.96  0.97  0.80];
  xGT = [0;0;1;0;0];
  b = [0.28; 0.55; 0.96];
  D = [-1 1 0 0 0;
       0 -1 1 0 0;
       0 0 -1 1 0;
       0 0 0 -1 1];
PetscBinaryWrite('cs1SparseMatrixA', A, 'precision', 'float64'); % do NOT need to convert A to sparse, always write as sparse matrix
[A2, b2, xGT2] = PetscBinaryRead('cs1Data_A_b_xGT');
% PetscBinaryWrite('cs1VecB', b, 'precision', 'float64');   
% PetscBinaryWrite('cs1VecXGT', xGT, 'precision', 'float64');
% PetscBinaryWrite('cs1Data_A_b_xGT', A, b, xGT, 'precision', 'float64');
% A2 = PetscBinaryRead('cs1SparseMatrixA');
% b2 = PetscBinaryRead('cs1VecB');
% xGT2 = PetscBinaryRead('cs1VecXGT');
%% Save data in petsc binary format, b = A*x
% save to one file
PetscBinaryWrite('tomographyData_A_b_xGT', L, S(:), WGT(:), 'precision', 'float64');
[A2, b2, xGT2] = PetscBinaryRead('tomographyData_A_b_xGT');
difference(full(A2), full(L));
difference(b2, S(:));
difference(xGT2, WGT(:));
% Save to separate files
% PetscBinaryWrite('tomographySparseMatrixA', L, 'precision', 'float64');
% PetscBinaryWrite('tomographyVecXGT', WGT(:), 'precision', 'float64');
% PetscBinaryWrite('tomographyVecB', S(:), 'precision', 'float64');

%% Below we shows the matlab reconstruction using 
isDemoMatLabReconstruction = 1; % 1/0
if isDemoMatLabReconstruction  
    %% Show W and S
    figNo = 10;
    figure(figNo+1); imagesc(WGT); axis image; title('Object Ground Truth');
    figure(figNo+2); imagesc(S); axis image; title('Sinogram');
    tilefigs;
    %% Reconstruction with solver from XH, with L1/TV regularizer.
    % Need 100/500/1000+ iteration to get good/very good/excellent result with small regularizer.
    % choose small maxSVDofA to make sure initial step size is not too small. 1.8e-6 and 1e-6 could make big difference for n=2 case
    regType = 'L1'; % 'TV' or 'L1' % TV is better and cleaner for phantom example        
    regWt = 1e-8*max(WGT(:)); % 1e-6 to 1e-8 both good for phantom, use 1e-8 for brain, especically WGT is scaled to maximum of 1 not 40
    maxIterA = 500; % 100 is not enough
    maxSVDofA = 1e-6; %svds(L, 1)*1e-4; % multiply by 1e-4 to make sure it is small enough so that first step in TwIST is long enough
    paraTwist = {'xSz', WSz, 'regFun', regType, 'regWt', regWt, 'isNonNegative', 1, 'maxIterA', maxIterA, 'xGT', WGT, 'maxSVDofA', maxSVDofA};
    WRec = solveTwist(S, L, paraTwist{:});    
    figure(figNo+3); W = WRec;  imagesc(W); axis image; title(sprintf('Rec twist, PSNR=%.2fdB, %s, regWt=%.1e, maxIter=%d', difference(W, WGT), regType, regWt, maxIterA));
    tilefigs;
    m
    % comparison of tao-brgn with matlab-twist
    xRec2 = PetscBinaryRead('tomographyResult_x');
    WRec2 = reshape(xRec2, Ny, Nx);
    figure, multAxes(@imagesc, {WGT, WRec2, WRec}); multAxes(@axis, 'image'); linkAxesXYZLimColorView; multAxes(@colorbar);
    multAxes(@title, {'Ground Truth', sprintf('Reconstruction-Tao-brgn,psnr=%.2fdB', psnr(WRec2, WGT)), sprintf('Reconstruction-Matlab-Twist, psnr=%.2fdB', psnr(WRec, WGT))});
end