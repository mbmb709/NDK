from pathlib import Path

p = Path("EcranViu/app/src/main/java/ro/ecranviu/PlaybackActivity.java")
s = p.read_text()
s = s.replace(
    "import android.graphics.drawable.AnimatedImageDrawable;",
    "import android.graphics.drawable.AnimatedImageDrawable;\nimport android.graphics.drawable.Animatable2;"
)
old = """                if(drawable instanceof AnimatedImageDrawable){animated=(AnimatedImageDrawable)drawable;animated.setRepeatCount(AnimatedImageDrawable.REPEAT_INFINITE);animated.start();}
                long limit=(time>0?time*1000L:gifDuration(uri));
                started=true;handler.postDelayed(endRunnable,Math.max(100L,limit));"""
new = """                if(drawable instanceof AnimatedImageDrawable){
                    animated=(AnimatedImageDrawable)drawable;
                    if(time<0){
                        animated.setRepeatCount(0);
                        animated.registerAnimationCallback(new Animatable2.AnimationCallback(){
                            @Override public void onAnimationEnd(Drawable drawable){
                                handler.post(() -> finishPlayback(true));
                            }
                        });
                    } else {
                        animated.setRepeatCount(AnimatedImageDrawable.REPEAT_INFINITE);
                    }
                    animated.start();
                    started=true;
                    if(time>0) handler.postDelayed(endRunnable,time*1000L);
                } else {
                    started=true;
                    handler.postDelayed(endRunnable,time>0?time*1000L:5000L);
                }"""
if old not in s:
    raise SystemExit("GIF code not found")
p.write_text(s.replace(old, new))
print("GIF single-cycle fix applied.")
