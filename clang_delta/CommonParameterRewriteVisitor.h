//===----------------------------------------------------------------------===//
//
// Copyright (c) 2012 The University of Utah
// All rights reserved.
//
// This file is distributed under the University of Illinois Open Source
// License.  See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef COMMON_PARAMETER_REWRITE_VISITOR_H
#define COMMON_PARAMETER_REWRITE_VISITOR_H

#include "llvm/ADT/SmallVector.h"
#include "clang/AST/RecursiveASTVisitor.h"

template<typename T, typename Trans>
class CommonParameterRewriteVisitor : public clang::RecursiveASTVisitor<T> {

public:
  explicit CommonParameterRewriteVisitor(Trans *Instance)
    : ConsumerInstance(Instance)
  { }

  bool VisitCallExpr(clang::CallExpr *E);

  bool VisitFunctionDecl(clang::FunctionDecl *FD);

  bool VisitCXXConstructorDecl(clang::CXXConstructorDecl *CD);

  bool VisitDeclRefExpr(clang::DeclRefExpr *ParmRefExpr);

  void rewriteAllExprs(void);

protected:

  clang::SmallVector<clang::CallExpr *, 10> AllCallExprs;

  clang::SmallVector<const clang::CXXConstructExpr *, 5> AllConstructExprs;

  bool rewriteFuncDecl(clang::FunctionDecl *FP);

  bool rewriteParam(const clang::ParmVarDecl *PV, 
                    unsigned int NumParams);

  bool rewriteOneCallExpr(clang::CallExpr *E);

  bool rewriteOneConstructExpr(const clang::CXXConstructExpr *CE);

  Trans *ConsumerInstance;
};

template<typename T, typename Trans>
bool CommonParameterRewriteVisitor<T, Trans>::rewriteParam(
       const clang::ParmVarDecl *PV, unsigned int NumParams)
{
  return ConsumerInstance->RewriteHelper->removeParamFromFuncDecl(PV, 
                                                NumParams,
                                                ConsumerInstance->TheParamPos);
}

template<typename T, typename Trans>
bool CommonParameterRewriteVisitor<T, Trans>::rewriteFuncDecl(
       clang::FunctionDecl *FD)
{
  const clang::ParmVarDecl *PV = 
    FD->getParamDecl(ConsumerInstance->TheParamPos);

  TransAssert(PV && "Unmatched ParamPos!");
  if (!rewriteParam(PV, FD->getNumParams()))
    return false;

  if (FD->isThisDeclarationADefinition()) {
    ConsumerInstance->TheParmVarDecl = PV;
    if (!ConsumerInstance->transformParamVar(FD, PV))
      return false;
  }
  return true;
}

template<typename T, typename Trans>
bool CommonParameterRewriteVisitor<T, Trans>::VisitCXXConstructorDecl(
       clang::CXXConstructorDecl *CD)
{
  for (clang::CXXConstructorDecl::init_iterator I = CD->init_begin(),
       E = CD->init_end(); I != E; ++I) {
    const clang::Expr *InitE = (*I)->getInit();
    if (!InitE)
      continue;

    const clang::CXXConstructExpr *CE = 
      llvm::dyn_cast<clang::CXXConstructExpr>(InitE);
    if (!CE)
      continue;

    const clang::CXXConstructorDecl *CtorD = CE->getConstructor();
    if (CtorD->getCanonicalDecl() == ConsumerInstance->TheFuncDecl)
      AllConstructExprs.push_back(CE);
  }

  return true;
}

template<typename T, typename Trans>
bool CommonParameterRewriteVisitor<T, Trans>::VisitFunctionDecl(
       clang::FunctionDecl *FD)
{
  clang::FunctionDecl *CanonicalFD = FD->getCanonicalDecl();

  if (CanonicalFD == ConsumerInstance->TheFuncDecl)
    return rewriteFuncDecl(FD);

  return true;
}

template<typename T, typename Trans>
bool CommonParameterRewriteVisitor<T, Trans>::rewriteOneCallExpr(
       clang::CallExpr *E)
{
  return ConsumerInstance->RewriteHelper->
           removeArgFromCallExpr(E, ConsumerInstance->TheParamPos);
}

template<typename T, typename Trans>
bool CommonParameterRewriteVisitor<T, Trans>::rewriteOneConstructExpr(
       const clang::CXXConstructExpr *CE)
{
  return ConsumerInstance->RewriteHelper->
           removeArgFromCXXConstructExpr(CE, ConsumerInstance->TheParamPos);
}

template<typename T, typename Trans>
void CommonParameterRewriteVisitor<T, Trans>::rewriteAllExprs(void)
{
  while (!AllCallExprs.empty()) {
    clang::CallExpr *CallE = AllCallExprs.pop_back_val();
    rewriteOneCallExpr(CallE);
  }

  while (!AllConstructExprs.empty()) {
    const clang::CXXConstructExpr *CE = AllConstructExprs.pop_back_val();
    rewriteOneConstructExpr(CE);
  }
}

template<typename T, typename Trans>
bool CommonParameterRewriteVisitor<T, Trans>::VisitCallExpr(
       clang::CallExpr *CallE)
{
  const clang::FunctionDecl *CalleeDecl = NULL;
  const clang::Expr *E = CallE->getCallee();
  if (const clang::UnresolvedLookupExpr *UE = 
      llvm::dyn_cast<clang::UnresolvedLookupExpr>(E)) {
    clang::DeclarationName DName = UE->getName();
    TransAssert((DName.getNameKind() == clang::DeclarationName::Identifier) &&
                "Not an indentifier!");
    if (const clang::NestedNameSpecifier *NNS = UE->getQualifier()) {
      if (const clang::DeclContext *Ctx = 
          ConsumerInstance->getDeclContextFromSpecifier(NNS))
        CalleeDecl = ConsumerInstance->lookupFunctionDecl(DName, Ctx);
    }
    if (!CalleeDecl)
      CalleeDecl = ConsumerInstance->lookupFunctionDecl(DName, 
                     ConsumerInstance->TheFuncDecl->getLookupParent());
    TransAssert(CalleeDecl && "NULL CalleeDecl!");
  }
  else {
    CalleeDecl = CallE->getDirectCallee();
    if (!CalleeDecl) {
      return true;
    }
  }

  if (clang::FunctionTemplateDecl *TheTmplFuncD = 
        ConsumerInstance->TheFuncDecl->getDescribedFunctionTemplate()) {
    clang::FunctionTemplateDecl *TmplFuncD;
    if (CalleeDecl->isTemplateInstantiation())
      TmplFuncD = CalleeDecl->getPrimaryTemplate();
    else 
      TmplFuncD = CalleeDecl->getDescribedFunctionTemplate();
    if (!TmplFuncD || 
        (TmplFuncD->getCanonicalDecl() != TheTmplFuncD->getCanonicalDecl()))
      return true;
  }
  else if (CalleeDecl->getCanonicalDecl() != ConsumerInstance->TheFuncDecl) {
    return true;
  }

  // We now have a correct CallExpr
  // Here we only collect these valid CallExprs, and 
  // will rewrite them later in a reverse order. 
  // The reason is that if we have code like below:
  //    foo(foo(1));
  // we want to rewrite the nested foo(1) first.
  // If we rewrite the outside foo first, we will
  // end up with bad transformation when we try to 
  // rewrite foo(1), which has been removed. 
  AllCallExprs.push_back(CallE);
  return true;
}

#endif
