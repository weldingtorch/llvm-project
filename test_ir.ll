define void @asd() {
  ret void
}

define i32 @foo() {
  %a = sub i32 5, 6
  %a1 = add i32 %a, 8
  %b = add i32 3, 9
  %c = add i32 %a1, %b
  ret i32 %c
}

define i32 @foou(i32 %n) {
entry:
  br label %header

header:                                           ; preds = %backedge, %entry
  %sum = phi i32 [ 0, %entry ], [ %sum.next, %backedge ]
  %i = phi i32 [ 0, %entry ], [ %i.next, %backedge ]
  %loop.cond = icmp slt i32 %i, %n
  br i1 %loop.cond, label %backedge, label %done

backedge:                                         ; preds = %header
  %n2 = mul nsw i32 %n, 2
  %i.plus.n2 = add nsw i32 %i, %n2
  %sum.next = add nsw i32 %sum, %i.plus.n2
  %i.next = add nsw i32 %i, 1
  br label %header

done:                                             ; preds = %header
  ret i32 %sum
}