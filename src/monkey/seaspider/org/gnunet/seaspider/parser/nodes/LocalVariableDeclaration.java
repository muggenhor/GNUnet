//
// Generated by JTB 1.3.2
//

package org.gnunet.seaspider.parser.nodes;

/**
 * Grammar production:
 * <PRE>
 * f0 -> [ &lt;STATIC&gt; ]
 * f1 -> VariableDeclaration()
 * </PRE>
 */
public class LocalVariableDeclaration implements Node {
   public NodeOptional f0;
   public VariableDeclaration f1;

   public LocalVariableDeclaration(NodeOptional n0, VariableDeclaration n1) {
      f0 = n0;
      f1 = n1;
   }

   public void accept(org.gnunet.seaspider.parser.visitors.Visitor v) {
      v.visit(this);
   }
   public <R,A> R accept(org.gnunet.seaspider.parser.visitors.GJVisitor<R,A> v, A argu) {
      return v.visit(this,argu);
   }
   public <R> R accept(org.gnunet.seaspider.parser.visitors.GJNoArguVisitor<R> v) {
      return v.visit(this);
   }
   public <A> void accept(org.gnunet.seaspider.parser.visitors.GJVoidVisitor<A> v, A argu) {
      v.visit(this,argu);
   }
}

