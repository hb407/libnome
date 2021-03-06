#include <cassert>
#include <algorithm>
#include "eri_ao2mo.h"
#include "wick.h"

namespace libgnme {

template<typename Tc, typename Tf, typename Tb>
void wick<Tc,Tf,Tb>::add_two_body(arma::Mat<Tb> &V)
{
    // Check input
    assert(V.n_rows == m_nbsf * m_nbsf);
    assert(V.n_cols == m_nbsf * m_nbsf);

    // Save two-body integrals
    m_II = V.memptr();

    // Setup control variable to indicate one-body initialised
    m_two_body = true;
}


template<typename Tc, typename Tf, typename Tb>
void wick<Tc,Tf,Tb>::setup_two_body()
{
    // Get dimensions of contractions
    size_t da = (m_nza > 0) ? 2 : 1;
    size_t db = (m_nzb > 0) ? 2 : 1;

    // Get view of two-electron AO integrals
    arma::Mat<Tb> IIao(m_II, m_nbsf*m_nbsf, m_nbsf*m_nbsf, false, true);

    // Initialise J/K matrices
    arma::field<arma::Mat<Tc> > Ja(da), Ka(da);
    for(size_t i=0; i<da; i++)
    {
        Ja(i).set_size(m_nbsf,m_nbsf); Ja(i).zeros();
        Ka(i).set_size(m_nbsf,m_nbsf); Ka(i).zeros();
    }
    arma::field<arma::Mat<Tc> > Jb(db), Kb(db);
    for(size_t i=0; i<db; i++)
    {
        Jb(i).set_size(m_nbsf,m_nbsf); Jb(i).zeros();
        Kb(i).set_size(m_nbsf,m_nbsf); Kb(i).zeros();
    }

    // Construct J/K matrices in AO basis
    for(size_t m=0; m < m_nbsf; m++)
    for(size_t n=0; n < m_nbsf; n++)
    {
        # pragma omp parallel for schedule(static) collapse(2)
        for(size_t s=0; s < m_nbsf; s++)
        for(size_t t=0; t < m_nbsf; t++)
        {
            for(size_t i=0; i<da; i++)
            {
                // Coulomb matrices
                Ja(i)(s,t) += IIao(m*m_nbsf+n,s*m_nbsf+t) * m_wxMa(i)(n,m); 
                // Exchange matrices
                Ka(i)(s,t) += IIao(m*m_nbsf+t,s*m_nbsf+n) * m_wxMa(i)(n,m);
            }
            for(size_t i=0; i<db; i++)
            {
                // Coulomb matrices
                Jb(i)(s,t) += IIao(m*m_nbsf+n,s*m_nbsf+t) * m_wxMb(i)(n,m); 
                // Exchange matrices
                Kb(i)(s,t) += IIao(m*m_nbsf+t,s*m_nbsf+n) * m_wxMb(i)(n,m);
            }
        }
    }

    // Alpha-Alpha V terms
    m_Vaa.resize(3); m_Vaa.zeros();
    m_Vaa(0) = arma::dot(Ja(0).st() - Ka(0).st(), m_wxMa(0));
    if(m_nza > 1)
    {
        m_Vaa(1) = 2.0 * arma::dot(Ja(0).st() - Ka(0).st(), m_wxMa(1));
        m_Vaa(2) = arma::dot(Ja(1).st() - Ka(1).st(), m_wxMa(1));
    }
    // Beta-Beta V terms
    m_Vbb.resize(3); m_Vbb.zeros();
    m_Vbb(0) = arma::dot(Jb(0).st() - Kb(0).st(), m_wxMb(0));
    if(m_nzb > 1)
    {
        m_Vbb(1) = 2.0 * arma::dot(Jb(0).st() - Kb(0).st(), m_wxMb(1));
        m_Vbb(2) = arma::dot(Jb(1).st() - Kb(1).st(), m_wxMb(1));
    }
    // Alpha-Beta V terms
    m_Vab.resize(da,db); m_Vab.zeros();
    for(size_t i=0; i<da; i++)
    for(size_t j=0; j<db; j++)
        m_Vab(i,j) = arma::dot(Ja(i).st(), m_wxMb(j));

    // Construct effective one-body terms
    // xx[Y(J-K)X]    xw[Y(J-K)Y]
    // wx[X(J-K)X]    ww[X(J-K)Y]
    m_XVaXa.set_size(da,da,da); 
    m_XVaXb.set_size(db,da,db);
    m_XVbXa.set_size(da,db,da); 
    m_XVbXb.set_size(db,db,db);
    for(size_t i=0; i<da; i++)
    for(size_t j=0; j<da; j++)
    for(size_t k=0; k<da; k++)
        m_XVaXa(i,k,j) = m_CXa(i).t() * (Ja(k) - Ka(k)) * m_XCa(j);
    for(size_t i=0; i<da; i++)
    for(size_t j=0; j<da; j++)
    for(size_t k=0; k<db; k++)
        m_XVbXa(i,k,j) = m_CXa(i).t() * Jb(k) * m_XCa(j);
    for(size_t i=0; i<db; i++)
    for(size_t j=0; j<db; j++)
    for(size_t k=0; k<da; k++)
        m_XVaXb(i,k,j) = m_CXb(i).t() * Ja(k) * m_XCb(j);
    for(size_t i=0; i<db; i++)
    for(size_t j=0; j<db; j++)
    for(size_t k=0; k<db; k++)
        m_XVbXb(i,k,j) = m_CXb(i).t() * (Jb(k) - Kb(k)) * m_XCb(j);

    // Build the two-electron integrals
    // Bra: xY    wX
    // Ket: xX    wY
    m_IIaa.set_size(da*da,da*da); // aa
    m_IIbb.set_size(db*db,db*db); // bb
    m_IIab.set_size(da*da,db*db); // ab
    m_IIba.set_size(db*db,da*da); // ba
    for(size_t i=0; i<da; i++)
    for(size_t j=0; j<da; j++)
    for(size_t k=0; k<da; k++)
    for(size_t l=0; l<da; l++)
    {
        // Initialise the memory
        m_IIaa(2*i+j, 2*k+l).resize(4*m_nact*m_nact, 4*m_nact*m_nact); 
        // Construct two-electron integrals
        eri_ao2mo(m_CXa(i), m_XCa(j), m_CXa(k), m_XCa(l), 
                  IIao, m_IIaa(2*i+j, 2*k+l), 2*m_nact, true); 
    }
    for(size_t i=0; i<db; i++)
    for(size_t j=0; j<db; j++)
    for(size_t k=0; k<db; k++)
    for(size_t l=0; l<db; l++)
    {
        // Initialise the memory
        m_IIbb(2*i+j, 2*k+l).resize(4*m_nact*m_nact, 4*m_nact*m_nact); 
        // Construct two-electron integrals
        eri_ao2mo(m_CXb(i), m_XCb(j), m_CXb(k), m_XCb(l), 
                  IIao, m_IIbb(2*i+j, 2*k+l), 2*m_nact, true); 
    }
    for(size_t i=0; i<da; i++)
    for(size_t j=0; j<da; j++)
    for(size_t k=0; k<db; k++)
    for(size_t l=0; l<db; l++)
    {
        // Initialise the memory
        m_IIab(2*i+j, 2*k+l).resize(4*m_nact*m_nact, 4*m_nact*m_nact); m_IIab(2*i+j, 2*k+l).zeros();
        // Construct two-electron integrals
        eri_ao2mo(m_CXa(i), m_XCa(j), m_CXb(k), m_XCb(l), 
                  IIao, m_IIab(2*i+j, 2*k+l), 2*m_nact, false); 
        // Also store the transpose for IIab as it will make access quicker later
        m_IIba(2*k+l, 2*i+j) = m_IIab(2*i+j, 2*k+l).st();
    }
}

template<typename Tc, typename Tf, typename Tb>
void wick<Tc,Tf,Tb>::same_spin_two_body(
    arma::umat xhp, arma::umat whp,
    Tc &V, bool alpha)
{
    // Zero the output
    V = 0.0;

    // Establish number of bra/ket excitations
    size_t nx = xhp.n_rows; // Bra excitations
    size_t nw = whp.n_rows; // Ket excitations

    // Get reference to number of zeros for this spin
    const size_t &nz = alpha ? m_nza : m_nzb; 

    // Dimensions of multiple contractions
    size_t d = (nz > 0) ? 2 : 1;

    // Check we don't have a non-zero element
    if(nz > nw + nx + 2) return;

    // Get reference to relevant contractions
    const arma::field<arma::Mat<Tc> > &X = alpha ? m_Xa : m_Xb;
    const arma::field<arma::Mat<Tc> > &Y = alpha ? m_Ya : m_Yb;
    // Get reference to relevant zeroth-order term
    const arma::Col<Tc> &V0  = alpha ? m_Vaa : m_Vbb;
    // Get reference to relevant J/K term
    const arma::field<arma::Mat<Tc> > &XVX = alpha ? m_XVaXa : m_XVbXb;
    // Get reference to relevant two-electron integrals
    arma::field<arma::Mat<Tc> > &II = alpha ? m_IIaa : m_IIbb;

    // TODO Correct indexing for new code
    whp += m_nact;

    // Get particle-hole indices
    arma::uvec rows, cols;
    if(nx == 0 xor nw == 0)
    {
        rows = (nx > 0) ? xhp.col(1) : whp.col(0);
        cols = (nx > 0) ? xhp.col(0) : whp.col(1);
    }
    else if(nx > 0 and nw > 0) 
    {
        rows = arma::join_cols(xhp.col(1),whp.col(0));
        cols = arma::join_cols(xhp.col(0),whp.col(1));
    }

    /* Generalised cases */
    // No excitations, so return simple overlap
    if(nx == 0 and nw == 0)
    {   
        V = V0(nz);
    }
    // One excitation doesn't require one-body determinant
    else if((nx+nw) == 1)
    {   
        // Distribute zeros over 3 contractions
        std::vector<size_t> m(nz, 1); m.resize(3,0);
        do {
            // Zeroth-order term
            V += V0(m[0] + m[1]) * X(m[2])(rows(0),cols(0));
            // First-order J/K term
            V -= 2.0 * XVX(m[0],m[1],m[2])(rows(0),cols(0));
        } while(std::prev_permutation(m.begin(), m.end()));
    }
    // Full generalisation!
    else
    {
        // Construct matrix for no zero overlaps
        arma::Mat<Tc> D  = arma::trimatl(X(0).submat(rows,cols))
                         + arma::trimatu(Y(0).submat(rows,cols),1);
        // Construct matrix with all zero overlaps
        arma::Mat<Tc> Db = arma::trimatl(X(1).submat(rows,cols)) 
                         + arma::trimatu(Y(1).submat(rows,cols),1);

        // Matrix of F contractions
        arma::field<arma::Mat<Tc> > JKtmp(d,d,d); 
        for(size_t i=0; i<d; i++)
        for(size_t j=0; j<d; j++)
        for(size_t k=0; k<d; k++)
            JKtmp(i,j,k) = XVX(i,j,k).submat(rows,cols);

        // Compute contribution from the overlap and zeroth term
        std::vector<size_t> m(nz, 1); m.resize(nx+nw+2, 0); 
        arma::Col<size_t> ind1(&m[2], nx+nw, false, true);
        arma::Col<size_t> ind2(&m[3], nx+nw-1, false, true);
        // Loop over all possible contributions of zero overlaps
        do {
            // Evaluate overlap contribution
            arma::Mat<Tc> Dtmp = D * arma::diagmat(1-ind1) + Db * arma::diagmat(ind1);
            
            // Get the overlap contributions 
            V += V0(m[0]+m[1]) * arma::det(Dtmp);
            
            // Get the effective one-body contribution
            // Loop over the column swaps for contracted terms
            for(size_t i=0; i < nx+nw; i++)
            {   
                // Take a safe copy of the column
                arma::Col<Tc> Dcol = Dtmp.col(i);
                // Make the swap
                Dtmp.col(i) = JKtmp(m[0],m[1],m[i+2]).col(i);
                // Add the one-body contribution
                V -= 2.0 * arma::det(Dtmp);
                // Restore the column
                Dtmp.col(i) = Dcol;
            }

            arma::field<arma::Mat<Tc> > IItmp(d);
            arma::Mat<Tc> D2, Db2, Dtmp2;
            // Loop over particle-hole pairs for two-body interaction
            for(size_t i=0; i < nx+nw; i++)
            for(size_t j=0; j < nx+nw; j++)
            {
                // Get temporary two-electron indices for this pair of electrons
                for(size_t x=0; x < d; x++)
                {
                    arma::Mat<Tc> vIItmp(
                        II(2*m[2]+x, 2*m[0]+m[1]).colptr(2*m_nact*rows(i)+cols(j)), 
                        2*m_nact, 2*m_nact, false, true);
                    IItmp(x) = vIItmp.submat(cols,rows).st();
                    IItmp(x).shed_row(i); 
                    IItmp(x).shed_col(j);
                }

                // New submatrices
                D2  = D;   D2.shed_row(i);  D2.shed_col(j);
                Db2 = Db; Db2.shed_row(i); Db2.shed_col(j);
                Dtmp2 = D2 * arma::diagmat(1-ind2) + Db2 * arma::diagmat(ind2);

                // Get the phase factor
                double phase = (i % 2) xor (j % 2) ? -1.0 : 1.0;

                // Loop over remaining columns
                for(size_t k=0; k < nx+nw-1; k++)
                {   
                    // Take a safe copy of the column
                    arma::Col<Tc> Dcol = Dtmp2.col(k);
                    // Make the swap
                    Dtmp2.col(k) = IItmp(m[k+3]).col(k);
                    // Add the one-body contribution
                    V += 0.5 * phase * arma::det(Dtmp2);
                    // Restore the column
                    Dtmp2.col(k) = Dcol;
                }
            }
        } while(std::prev_permutation(m.begin(), m.end()));
    }

    // TODO Correct indexing for old code
    whp -= m_nact;
}


template<typename Tc, typename Tf, typename Tb>
void wick<Tc,Tf,Tb>::diff_spin_two_body(
    arma::umat xahp, arma::umat xbhp,
    arma::umat wahp, arma::umat wbhp,
    Tc &V)
{
    // Zero the output
    V = 0.0;

    // Establish number of bra/ket excitations
    size_t nxa = xahp.n_rows; // Bra alpha excitations
    size_t nwa = wahp.n_rows; // Ket alpha excitations
    size_t nxb = xbhp.n_rows; // Bra beta excitations
    size_t nwb = wbhp.n_rows; // Ket beta excitations
    size_t nx = nxa + nxb;
    size_t nw = nwa + nwb;

    // Dimensions of multiple contractions
    size_t da = (m_nza > 0) ? 2 : 1;
    size_t db = (m_nzb > 0) ? 2 : 1;

    // Check we don't have a non-zero element
    if(m_nza > nwa+nxa+1 || m_nzb > nwb+nxb+1) return;

    // TODO Correct indexing for new code
    wahp += m_nact;
    wbhp += m_nact;

    // Get alpha particle-hole indices
    arma::uvec rowa, cola;
    if(nxa == 0 xor nwa == 0) 
    {
        rowa = (nxa > 0) ? xahp.col(1) : wahp.col(0);
        cola = (nxa > 0) ? xahp.col(0) : wahp.col(1);
    } 
    else if(nxa > 0 and nwa > 0) 
    {
        rowa = arma::join_cols(xahp.col(1),wahp.col(0));
        cola = arma::join_cols(xahp.col(0),wahp.col(1));
    }
    // Get beta particle-hole indices
    arma::uvec rowb, colb;
    if(nxb == 0 xor nwb == 0)
    {
        rowb = (nxb > 0) ? xbhp.col(1) : wbhp.col(0);
        colb = (nxb > 0) ? xbhp.col(0) : wbhp.col(1);
    }
    else if(nxb > 0 and nwb > 0) 
    {
        rowb = arma::join_cols(xbhp.col(1),wbhp.col(0));
        colb = arma::join_cols(xbhp.col(0),wbhp.col(1));
    }

    /* Super generalised case */
    arma::Mat<Tc> Da, DaB;
    if(nxa+nwa == 1)
    {
        Da  = m_Xa(0)(rowa(0),cola(0));
        DaB = m_Xa(1)(rowa(0),cola(0));
    }
    else if(nxa+nwa > 1)
    {
        // Construct matrix for no zero overlaps
        Da  = arma::trimatl(m_Xa(0).submat(rowa,cola))
            + arma::trimatu(m_Ya(0).submat(rowa,cola),1);
        // Construct matrix with all zero overlaps
        DaB = arma::trimatl(m_Xa(1).submat(rowa,cola)) 
            + arma::trimatu(m_Ya(1).submat(rowa,cola),1);
    }
    arma::Mat<Tc> Db, DbB;
    if(nxb+nwb == 1)
    {
        Db  = m_Xb(0)(rowb(0),colb(0));
        DbB = m_Xb(1)(rowb(0),colb(0));
    }
    else if(nxb+nwb > 1)
    {
        // Construct matrix for no zero overlaps
        Db  = arma::trimatl(m_Xb(0).submat(rowb,colb))
            + arma::trimatu(m_Yb(0).submat(rowb,colb),1);
        // Construct matrix with all zero overlaps
        DbB = arma::trimatl(m_Xb(1).submat(rowb,colb)) 
            + arma::trimatu(m_Yb(1).submat(rowb,colb),1);
    }

    // Matrix of JK contractions
    arma::field<arma::Mat<Tc> > Jab(db,da,db), Jba(da,db,da);
    for(size_t i=0; i<da; i++)
    for(size_t j=0; j<da; j++)
    for(size_t k=0; k<db; k++)
        Jba(i,k,j) = m_XVbXa(i,k,j).submat(rowa,cola);
    for(size_t i=0; i<db; i++)
    for(size_t j=0; j<db; j++)
    for(size_t k=0; k<da; k++)
        Jab(i,k,j) = m_XVaXb(i,k,j).submat(rowb,colb);

    // Compute contribution from the overlap and zeroth term
    std::vector<size_t> ma(m_nza, 1); ma.resize(nxa+nwa+1, 0); 
    std::vector<size_t> mb(m_nzb, 1); mb.resize(nxb+nwb+1, 0); 
    arma::Col<size_t> inda1(&ma[1], nxa+nwa,   false, true);
    arma::Col<size_t> inda2(&ma[2], nxa+nwa-1, false, true);
    arma::Col<size_t> indb1(&mb[1], nxb+nwb,   false, true);
    arma::Col<size_t> indb2(&mb[2], nxb+nwb-1, false, true);
    // Loop over all possible contributions of zero overlaps
    arma::Mat<Tc> tmpDa, tmpDa2;
    arma::Mat<Tc> tmpDb, tmpDb2;
    do {
    do {
        // Evaluate overlap contribution
        tmpDa = Da * arma::diagmat(1-inda1) + DaB * arma::diagmat(inda1);
        tmpDb = Db * arma::diagmat(1-indb1) + DbB * arma::diagmat(indb1);
        
        // Get the zeroth-order contributions 
        V += m_Vab(ma[0],mb[0]) * arma::det(tmpDa) * arma::det(tmpDb);

        // Get the effective one-body contribution
        // Loop over the alpha column swaps for contracted terms
        for(size_t i=0; i < nxa+nwa; i++)
        {   
            // Take a safe copy of the column
            arma::Col<Tc> Dcol = tmpDa.col(i);
            // Make the swap
            tmpDa.col(i) = Jba(ma[0],mb[0],ma[i+1]).col(i);
            // Add the one-body contribution
            V -= arma::det(tmpDa) * arma::det(tmpDb);
            // Restore the column
            tmpDa.col(i) = Dcol;
        }
        // Loop over the beta column swaps for contracted terms
        for(size_t i=0; i < nxb+nwb; i++)
        {   
            // Take a safe copy of the column
            arma::Col<Tc> Dcol = tmpDb.col(i);
            // Make the swap
            tmpDb.col(i) = Jab(mb[0],ma[0],mb[i+1]).col(i);
            // Add the one-body contribution
            V -= arma::det(tmpDa) * arma::det(tmpDb);
            // Restore the column
            tmpDb.col(i) = Dcol;
        }

        arma::field<arma::Mat<Tc> > IItmp(std::max(da,db));
        arma::Mat<Tc> D2, DB2;
        // Loop over alpha particle-hole pairs for two-body interaction
        for(size_t i=0; i < nxa+nwa; i++)
        for(size_t j=0; j < nxa+nwa; j++)
        {
            // Get temporary two-electron indices for this pair of electrons
            for(size_t x=0; x<db; x++)
            {
                arma::Mat<Tc> vIItmp(
                    m_IIba(2*mb[0]+x, 2*ma[0]+ma[1]).colptr(2*m_nact*rowa(i)+cola(j)), 
                    2*m_nact, 2*m_nact, false, true);
                IItmp(x) = vIItmp.submat(colb,rowb).st();
            }

            // New submatrices
            D2  = Da;   D2.shed_row(i);  D2.shed_col(j);
            DB2 = DaB; DB2.shed_row(i); DB2.shed_col(j);
            tmpDa2 = D2 * arma::diagmat(1-inda2) + DB2 * arma::diagmat(inda2);

            // Get the phase factor
            double phase = (i % 2) xor (j % 2) ? -1.0 : 1.0;

            // Loop over beta columns
            for(size_t k=0; k < nxb+nwb; k++)
            {   
                // Take a safe copy of the column
                arma::Col<Tc> Dcol = tmpDb.col(k);
                // Make the swap
                tmpDb.col(k) = IItmp(mb[k+1]).col(k);
                // Add the one-body contribution
                V += 0.5 * phase * arma::det(tmpDa2) * arma::det(tmpDb);
                // Restore the column
                tmpDb.col(k) = Dcol;
            }
        }
        // Loop over beta particle-hole pairs for two-body interaction
        for(size_t i=0; i < nxb+nwb; i++)
        for(size_t j=0; j < nxb+nwb; j++)
        {
            // Get temporary two-electron indices for this pair of electrons
            for(size_t x=0; x<da; x++)
            {
                arma::Mat<Tc> vIItmp(
                    m_IIab(2*ma[0]+x, 2*mb[0]+mb[1]).colptr(2*m_nact*rowb(i)+colb(j)), 
                    2*m_nact, 2*m_nact, false, true);
                IItmp(x) = vIItmp.submat(cola,rowa).st();
            }

            // New submatrices
            D2  = Db;   D2.shed_row(i);  D2.shed_col(j);
            DB2 = DbB; DB2.shed_row(i); DB2.shed_col(j);
            tmpDb2 = D2 * arma::diagmat(1-indb2) + DB2 * arma::diagmat(indb2);

            // Get the phase factor
            double phase = (i % 2) xor (j % 2) ? -1.0 : 1.0;

            // Loop over alpha columns
            for(size_t k=0; k < nxa+nwa; k++)
            {   
                // Take a safe copy of the column
                arma::Col<Tc> Dcol = tmpDa.col(k);
                // Make the swap
                tmpDa.col(k) = IItmp(ma[k+1]).col(k);
                // Add the one-body contribution
                V += 0.5 * phase * arma::det(tmpDa) * arma::det(tmpDb2);
                // Restore the column
                tmpDa.col(k) = Dcol;
            }
        }
    } while(std::prev_permutation(ma.begin(), ma.end()));
    } while(std::prev_permutation(mb.begin(), mb.end()));
    
    // TODO Correct indexing for old code
    wahp -= m_nact;
    wbhp -= m_nact;
}

template class wick<double, double, double>;
template class wick<std::complex<double>, double, double>;
template class wick<std::complex<double>, std::complex<double>, double>;
template class wick<std::complex<double>, std::complex<double>, std::complex<double> >;

} // namespace libgnme
