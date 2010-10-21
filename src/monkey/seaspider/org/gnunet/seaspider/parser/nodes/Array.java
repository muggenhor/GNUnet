//
// Generated by JTB 1.3.2
//

package org.gnunet.seaspider.parser.nodes;

/**
 * Grammar production:
 * <PRE>
 * f0 -> "["
 * f1 -> [ ConstantExpression() ]
 * f2 -> "]"
 * </PRE>
 */
public class Array implements Node {
   public NodeToken f0;
   public NodeOptional f1;
   public NodeToken f2;

   public Array(NodeToken n0, NodeOptional n1, NodeToken n2) {
      f0 = n0;
      f1 = n1;
      f2 = n2;
   }

   public Array(NodeOptional n0) {
      f0 = new NodeToken("[");
      f1 = n0;
      f2 = new NodeToken("]");
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

